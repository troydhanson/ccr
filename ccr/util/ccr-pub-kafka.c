/*
 * kafka publisher
 *
 */

#define _GNU_SOURCE /* asprintf */
#include <sys/signalfd.h>
#include <sys/epoll.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include "ccr.h"

#include <librdkafka/rdkafka.h>

#define NUM_IOV 100000
#define BUF_LEN (NUM_IOV * 1000)
struct kafka {
  char *topic;
  rd_kafka_t *k;
  rd_kafka_topic_t *t;
  rd_kafka_conf_t *conf;
  rd_kafka_topic_conf_t *topic_conf;
};

struct pub {
  char *ring_name;
  int fd;
  char batch_end;      /* 1 means we sent the last record */
  char batch_end_ackd; /* 1 means kakfa ackd its delivery */
  unsigned sent;
  unsigned ackd;
  struct kafka k;
  struct ccr *ring;

  /* read buffer, for ccr read */
  struct iovec ccr_iov[NUM_IOV];
  char ccr_buf[BUF_LEN];

  /* push buffer, for kafka queues */
  struct iovec out_iov[NUM_IOV];
  size_t out_niov;
  char *out_buf;
  size_t buf_size;
  size_t buf_used;
};

struct {
  int verbose;
  int batch_mode;
  char *prog;
  char *broker;
  int signal_fd;
  int epoll_fd;
  int ticks;
  int json;
  int pretty;
  int num_pub;
  struct pub *pubv;
  int shutdown;
  int signal_ppid;
} cfg = {
  .signal_fd = -1,
  .epoll_fd = -1,
};

void usage() {
  fprintf(stderr,"usage: %s [options] <ring>[:topic] ...\n", cfg.prog);
  fprintf(stderr,"\n");
  fprintf(stderr,"  -b <broker>[:port]   kafka server\n");
  fprintf(stderr,"  -v                   verbose\n");
  fprintf(stderr,"  -j                   json\n");
  fprintf(stderr,"  -p                   pretty json\n");
  fprintf(stderr,"  -B                   batch mode\n");
  fprintf(stderr,"  -P                   signal parent on batch end\n");
  fprintf(stderr,"  -h                   this help\n");
  fprintf(stderr,"\n");
  exit(-1);
}

/* signals that we'll accept via signalfd in epoll */
int sigs[] = {SIGHUP,SIGTERM,SIGINT,SIGQUIT,SIGALRM};

void hexdump(char *buf, size_t len) {
  size_t i,n=0;
  unsigned char c;
  while(n < len) {
    fprintf(stderr,"%08x ", (int)n);
    for(i=0; i < 16; i++) {
      c = (n+i < len) ? buf[n+i] : 0;
      if (n+i < len) fprintf(stderr,"%.2x ", c);
      else fprintf(stderr, "   ");
    }
    for(i=0; i < 16; i++) {
      c = (n+i < len) ? buf[n+i] : ' ';
      if (c < 0x20 || c > 0x7e) c = '.';
      fprintf(stderr,"%c",c);
    }
    fprintf(stderr,"\n");
    n += 16;
  }
}

int drain_callbacks() {
  struct pub *p;
  int i, n=0;

  for(i=0; i < cfg.num_pub; i++) {
    p = &cfg.pubv[i];
    do {
       n = rd_kafka_poll(p->k.k, 0);
    } while (n > 0);
  }

  return n;
}


int periodic_work() {
  int rc  = -1, sc, complete=0;

  sc = drain_callbacks();
  if (sc < 0) goto done;

  if (cfg.shutdown) {
    fprintf(stderr, "inducing shutdown\n");
    goto done;
  }

  if (complete && cfg.signal_ppid) {
    fprintf(stderr, "batch end: signaling parent\n");
    sc = kill(getppid(), SIGTERM);
    if (sc < 0) {
      fprintf(stderr, "error: %s\n", strerror(errno));
      goto done;
    }
  }

  rc = 0;
 
 done:
  return rc;
}

int new_epoll(int events, int fd) {
  int rc;
  struct epoll_event ev;
  memset(&ev,0,sizeof(ev));
  ev.events = events;
  ev.data.fd= fd;
  rc = epoll_ctl(cfg.epoll_fd, EPOLL_CTL_ADD, fd, &ev);
  if (rc == -1) {
    fprintf(stderr,"epoll_ctl: %s\n", strerror(errno));
  }
  return rc;
}

int mod_epoll(int events, int fd) {
  int rc;
  struct epoll_event ev;
  memset(&ev,0,sizeof(ev));
  ev.events = events;
  ev.data.fd= fd;
  rc = epoll_ctl(cfg.epoll_fd, EPOLL_CTL_MOD, fd, &ev);
  if (rc == -1) {
    fprintf(stderr,"epoll_ctl: %s\n", strerror(errno));
  }
  return rc;
}


void err_cb (rd_kafka_t *rk, int err, const char *reason, void *opaque) {

  fprintf(stderr,"librdkafka: error, %s %s: %s\n",
    rd_kafka_name(rk), rd_kafka_err2str(err), reason);

  cfg.shutdown=1;
}

/* delivery report callback gets invoked for every message */
void delivery_report_cb ( rd_kafka_t *rk, const rd_kafka_message_t *msg,
  void *opaque) {
  struct pub *p = (struct pub*)opaque;
  int sc;

  if (msg->err != 0) {
    fprintf(stderr, "librdkafka: message delivery failure: %s\n",
      rd_kafka_err2str(msg->err));
    cfg.shutdown=1;
    return;
  }

  /* successfully delivered message */
  p->ackd++;

  /* restore ring epoll once vented */
  if (p->ackd == p->sent) {
    if (p->batch_end) p->batch_end_ackd=1;
    sc = mod_epoll(EPOLLIN, p->fd);
    if (sc < 0) cfg.shutdown=1;
    //fprintf(stderr, "ring epoll reinstated\n");
  }
}


int send_kafka(struct pub *p) {
  int i, rc = -1, sc, msgflags=0;
  const char *serr;
  size_t len;
  char *msg;

  assert(p->out_niov > 0);

  /* publish one message at a time
   *
   * librdkafka returns an "error" if the producer queue
   * is full, which requires draining using rd_kafka_poll
   * and retrying (c.f. examples/rdkafka_simple_producer.c)
   */
  i = 0;
  while (i < p->out_niov) {

    /* let it invoke callbacks */
    rd_kafka_poll(p->k.k, 0);

    /* we're using iov_base as an offset here */
    msg = (size_t)(p->out_iov[i].iov_base) + p->out_buf;
    len = p->out_iov[i].iov_len;
    if (cfg.verbose) hexdump(msg, len);

    sc = rd_kafka_produce(p->k.t, RD_KAFKA_PARTITION_UA,
         msgflags, msg, len, NULL, 0, NULL);
    if (sc == 0) {
      p->sent++;
      i++;
      continue;
    }

    /* error: momentary queue-full or fatal error */
    assert(sc < 0);
#if RD_KAFKA_VERSION < 0x00090100
    if (errno == ENOBUFS)
      continue;
    serr = rd_kafka_err2str( rd_kafka_errno2err(errno) );
    fprintf(stderr, "rd_kafka_produce: %s\n", serr);
    goto done;
#else
    if (rd_kafka_last_error() == RD_KAFKA_RESP_ERR__QUEUE_FULL)
      continue;
    serr = rd_kafka_err2str( rd_kafka_last_error() );
    fprintf(stderr, "rd_kafka_produce: %s\n", serr);
    goto done;
#endif
  }

  rc = 0;

 done:
  return rc;
}

int open_kafka(struct kafka *k, void *opaque) {
  char err[512];
  int rc=-1, kr;

  k->conf = rd_kafka_conf_new();
  rd_kafka_conf_set_opaque(k->conf, opaque);
  rd_kafka_conf_set_error_cb(k->conf, err_cb);
  rd_kafka_conf_set_dr_msg_cb(k->conf, delivery_report_cb);

  /* request library accumulates for X milliseconds before transmit */
  kr = rd_kafka_conf_set(k->conf, "queue.buffering.max.ms", "100",
      err, sizeof(err));
  if (kr != RD_KAFKA_CONF_OK) {
    fprintf(stderr,"rd_kafka_topic_conf_set: %s\n", err);
    goto done;
  }

  /* request large batches before transmit */
  kr = rd_kafka_conf_set(k->conf, "batch.num.messages", "100000",
      err, sizeof(err));
  if (kr != RD_KAFKA_CONF_OK) {
    fprintf(stderr,"rd_kafka_topic_conf_set: %s\n", err);
    goto done;
  }

  k->topic_conf = rd_kafka_topic_conf_new();

  k->k = rd_kafka_new(RD_KAFKA_PRODUCER, k->conf, err, sizeof(err));
  if (k->k == NULL) {
    fprintf(stderr, "rd_kafka_new: %s\n", err);
    goto done;
  }

  if (rd_kafka_brokers_add(k->k, cfg.broker) < 1) {
    fprintf(stderr, "error adding broker %s\n", cfg.broker);
    goto done;
  }

  k->t = rd_kafka_topic_new(k->k, k->topic, k->topic_conf);
  if (k->t == NULL) {
    fprintf(stderr, "error creating topic %s\n", k->topic);
    goto done;
  }

  rc = 0;

 done:
  return rc;
}

int handle_signal() {
  struct signalfd_siginfo info;
  int sc, rc=-1;
  ssize_t nr;
  char *s;
  
  nr = read(cfg.signal_fd, &info, sizeof(info));
  if (nr != sizeof(info)) {
    fprintf(stderr,"failed to read signal fd buffer\n");
    goto done;
  }

  switch(info.ssi_signo) {
    case SIGALRM: 
      sc = periodic_work();
      if (sc < 0) goto done;
      alarm(1); 
      break;
    default: 
      s = strsignal(info.ssi_signo);
      fprintf(stderr,"got signal %d (%s)\n", info.ssi_signo, s);
      goto done;
      break;
  }

 rc = 0;

 done:
  return rc;
}

/* test if fd belongs to an open ring.
 * if so, return 1 and store its ccr* into r */
int is_ring(int fd, struct pub **p) {
  int i;

  for(i=0; i < cfg.num_pub; i++) {
    if (cfg.pubv[i].fd != fd)
      continue;

    *p = &cfg.pubv[i];
    return 1;
  }

  return 0;
}

/* called when ring is readable */
int handle_ring(struct pub *p) {
  size_t niov, i, l, len;
  int rc = -1, fl, sc;
  char *b, *out;
  struct ccr *r;
  struct cc *cc;
  ssize_t nr;
  void *tmp;

  r = p->ring;
  niov = NUM_IOV;
  nr = ccr_readv(r, 0, p->ccr_buf, BUF_LEN, p->ccr_iov, &niov);

  if (nr <= 0) {
    if (nr) fprintf(stderr, "ccr_readv: error %zd\n", nr);
    else rc = 0;
    goto done;
  }

  assert( nr > 0 );
  assert( niov > 0 );

  cc = ccr_get_cc( r );
  fl = cfg.pretty ? CC_PRETTY : 0;

  p->out_niov = 0;
  p->buf_used = 0;
  for (i=0; i < niov; i++) {

    b = p->ccr_iov[i].iov_base;
    l = p->ccr_iov[i].iov_len;

    /* sets p->batch_end as record indicates */
    if (cfg.batch_mode) cc_restore(cc, b, l, 0);

    if (cfg.json) {
      sc = cc_to_json(cc, &out, &len, b, l, fl);
      if (sc < 0) {
        fprintf(stderr, "json conversion failed\n");
        goto done;
      }
    } else {
      out = b;
      len = l;
    }

    if (p->buf_size - p->buf_used < len) {
      p->buf_size = p->buf_size ? (p->buf_size * 2) : (len * NUM_IOV);
      tmp = realloc(p->out_buf, p->buf_size);
      if (tmp == NULL) {
        fprintf(stderr, "out of memory\n");
        goto done;
      }
      p->out_buf = tmp;
    }
    memcpy(p->out_buf + p->buf_used, out, len);
    p->out_iov[i].iov_base = (char*)p->buf_used; /* offset! */
    p->out_iov[i].iov_len = len;
    p->buf_used += len;
    p->out_niov++;
  }

  /* suspend ring epoll while buffer vents */
  sc = mod_epoll(0, p->fd);
  if (sc < 0) goto done;
  //fprintf(stderr, "ring epoll suspended\n");

  /* queue entire output */
  sc = send_kafka(p);
  if (sc < 0) goto done;

  rc = 0;

 done:
  return rc;
}

int main(int argc, char *argv[]) {
  char *ring, *topic, *colon;
  struct epoll_event ev;
  cfg.prog = argv[0];
  int fd, i, sc, opt;
  struct ccr *r;
  struct pub *p;
  unsigned n;

  while ( (opt=getopt(argc,argv,"b:BvhjpP")) != -1) {
    switch(opt) {
      case 'v': cfg.verbose++; break;
      case 'b': cfg.broker = strdup(optarg); break;
      case 'B': cfg.batch_mode=1; break;
      case 'j': cfg.json=1; break;
      case 'p': cfg.json=1; cfg.pretty=1; break;
      case 'P': cfg.signal_ppid=1; break;
      case 'h': default: usage(); break;
    }
  }

  if (cfg.broker == NULL) usage();

  /* block all signals. we take signals synchronously via signalfd */
  sigset_t all;
  sigfillset(&all);
  sigprocmask(SIG_SETMASK,&all,NULL);

  /* a few signals we'll accept via our signalfd */
  sigset_t sw;
  sigemptyset(&sw);
  for(n=0; n < sizeof(sigs)/sizeof(*sigs); n++) sigaddset(&sw, sigs[n]);

  /* create the signalfd for receiving signals */
  cfg.signal_fd = signalfd(-1, &sw, 0);
  if (cfg.signal_fd == -1) {
    fprintf(stderr,"signalfd: %s\n", strerror(errno));
    goto done;
  }

  /* set up the epoll instance */
  cfg.epoll_fd = epoll_create(1); 
  if (cfg.epoll_fd == -1) {
    fprintf(stderr,"epoll: %s\n", strerror(errno));
    goto done;
  }

  /* add descriptors of interest */
  if (new_epoll(EPOLLIN, cfg.signal_fd))   goto done;

  /* rings from command line */
  cfg.num_pub = argc - optind;
  if (cfg.num_pub == 0) usage();

  cfg.pubv = calloc(cfg.num_pub, sizeof(struct pub));
  if (cfg.pubv == NULL) goto done;

  for(i=0; optind < argc; i++, optind++) {

    p = &cfg.pubv[i];
    ring = strdup( argv[optind] );
    colon = strchr(ring, ':');
    if (colon) *colon = '\0';
    topic = colon ? (colon+1) : ring;
    p->k.topic = topic;
    p->ring_name = ring;

    r = ccr_open( ring, CCR_RDONLY|CCR_NONBLOCK);
    if (r == NULL) goto done;
    cfg.pubv[i].ring = r;

    fd = ccr_get_selectable_fd( r );
    if (fd < 0) goto done;
    cfg.pubv[i].fd = fd;

    sc = new_epoll(EPOLLIN, fd);
    if (sc < 0) goto done;

    struct cc_map map[] = {{"batch_end", CC_i8, &p->batch_end}};
    sc = ccr_mapv(r, map, 1);
    if (sc < 0) goto done;

    sc = open_kafka( &p->k, p );
    if (sc < 0) goto done;
  }


  alarm(1);
  for (;;) {

    sc = epoll_wait(cfg.epoll_fd, &ev, 1, -1);
    if (sc < 0) {
      fprintf(stderr,"epoll: %s\n", strerror(errno));
      break;
    }

    if (ev.data.fd == cfg.signal_fd) {
      sc = handle_signal();
      if (sc < 0) goto done;
    } 
    else if (is_ring(ev.data.fd, &p)) {
      sc = handle_ring(p);
      if (sc < 0) goto done;
    }
    else {
      fprintf(stderr, "unknown fd\n");
      assert(0);
    }

  }

done:
  for(i=0; cfg.pubv && (i < cfg.num_pub); i++) {
    p = &cfg.pubv[ i ];
    if (p->ring_name) free(p->ring_name);
    if (p->ring) ccr_close( p->ring );
    if (p->out_buf) free(p->out_buf);
    /* do not close p->fd */
  }
  if (cfg.pubv) free(cfg.pubv);
  if (cfg.broker) free(cfg.broker);
  if (cfg.epoll_fd != -1) close(cfg.epoll_fd);
  if (cfg.signal_fd != -1) close(cfg.signal_fd);
  return 0;
}
