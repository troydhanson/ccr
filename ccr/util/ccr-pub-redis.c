/*
 * redis publisher
 *
 */

#include <sys/signalfd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <sys/un.h>
#include <errno.h>
#include <stdio.h>
#include <netdb.h>
#include <time.h>

#include "ccr.h"

/* redis related defaults */
#define DEFAULT_PORT 6379
#define DEFAULT_UNIX "/var/run/redis/redis.sock"
#define DEFAULT_VERB "PUBLISH"
#define MAX_RESP 2000 /* cmd e.g. publish foo bar */

#define NUM_IOV 100000
#define BUF_LEN (NUM_IOV * 1000)
struct redis {
  char from[BUF_LEN];
  size_t left;
  char *key;
  int key_len;
  int fd;
};

struct pub {
  char *ring_name;
  int fd;
  unsigned sent;
  unsigned ackd;
  struct redis k;
  struct ccr *ring;

  /* read buffer, for ccr read */
  struct iovec ccr_iov[NUM_IOV];
  char ccr_buf[BUF_LEN];

  /* push buffer, for redis output */
  char *out_buf;
  size_t buf_sent;
  size_t buf_size;
  size_t buf_used;
};

struct {
  int verbose;
  char *prog;

  /* transport is either tcp host/port
   * or unix domain socket (preferred) */
  enum { TRANSPORT_TCP, TRANSPORT_UNIX } transport;
  struct sockaddr_in in;
  struct sockaddr_un un;
  char *unix_socket;
  char *host;

  int signal_fd;
  int epoll_fd;
  int ticks;
  int json;
  int pretty;
  int num_pub;
  struct pub *pubv;
  int signal_ppid;
  struct redis sk;
  char *verb;
  int verb_len;
  char resp[MAX_RESP];
} cfg = {
  .signal_fd = -1,
  .epoll_fd = -1,
  .transport = TRANSPORT_UNIX,
  .unix_socket = DEFAULT_UNIX,
  .verb = DEFAULT_VERB,
  .verb_len = sizeof(DEFAULT_VERB)-1,
};

void usage() {
  fprintf(stderr,"usage: %s [options] <ring>[:channel] ...\n", cfg.prog);
  fprintf(stderr,"\n");
  fprintf(stderr,"  -U                   connect using unix socket (default)\n");
  fprintf(stderr,"  -b <host>[:port]     connect using TCP socket\n");
  fprintf(stderr,"  -u <redis-socket>    unix socket (default: %s)\n", DEFAULT_UNIX);
  fprintf(stderr,"  -V <verb>            redis mode (default: %s)\n", DEFAULT_VERB);
  fprintf(stderr,"  -v                   verbose\n");
  fprintf(stderr,"  -j                   json\n");
  fprintf(stderr,"  -p                   pretty json\n");
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

/* forms a volatile redis resp-formatted buffer.
 * caller must copy or use it immediately! */
int form_resp(char **out, char *key, size_t keylen, char *arg, size_t arglen) {
  int len;

  len = snprintf(cfg.resp, MAX_RESP,
                "*3\r\n"   /* array of length 3  */
                "$%d\r\n"  /* strlen(verb)       */
                "%s\r\n"   /* verb               */
                "$%zu\r\n" /* strlen(key)        */
                "%s\r\n"   /* key                */
                "$%zu\r\n",/* arglen             */
                cfg.verb_len,
                cfg.verb,
                keylen,
                key,
                arglen);

  if (len + arglen + 2 >= MAX_RESP) {
    fprintf(stderr, "RESP buffer too small\n");
    *out = NULL;
    return 0;
  }

  /* the argument can contain binary \0 so we
   * write it using memcpy rather than printf */
  memcpy(cfg.resp + len, arg, arglen);
  memcpy(cfg.resp + len + arglen, "\r\n", 2);
  len += arglen + 2;

  if (cfg.verbose) {
    fprintf(stderr, "resp:\n");
    hexdump(cfg.resp, len);
  }

  *out = cfg.resp;
  return len;
}


int periodic_work() {
  int rc = -1;

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

int send_redis(struct pub *p, int *vented) {
  int rc = -1;
  ssize_t nr;
  size_t l;
  char *o;

  assert(p->buf_sent < p->buf_used);

  o = p->out_buf + p->buf_sent;
  l = p->buf_used - p->buf_sent;

  nr = write(p->k.fd, o, l);
  if (nr < 0) {
    fprintf(stderr, "write: %s\n", strerror(errno));
    goto done;
  }

  p->buf_sent += nr;
  *vented = (p->buf_sent < p->buf_used) ? 0 : 1;
  rc = 0;

 done:
  return rc;
}

int open_redis(struct redis *r) {
  int rc=-1, fd=-1, sc, domain, sl;
  struct sockaddr *sa;
  socklen_t sz;

  fprintf(stderr, "connecting to %s\n",
    (cfg.transport == TRANSPORT_TCP) ?
    cfg.host : cfg.unix_socket);

  switch(cfg.transport) {
    case TRANSPORT_TCP:
      domain = AF_INET;
      sa = (struct sockaddr*)&cfg.in;
      sz = sizeof(struct sockaddr_in);
      break;
    case TRANSPORT_UNIX:
      domain = AF_UNIX;
      sa = (struct sockaddr*)&cfg.un;
      sz = sizeof(struct sockaddr_un);
      sl = strlen(cfg.unix_socket);
      cfg.un.sun_family = AF_UNIX;
      if (sl + 1 > sizeof(cfg.un.sun_path)) {
        fprintf(stderr, "socket path too long\n");
        goto done;
      }
      memcpy(cfg.un.sun_path, cfg.unix_socket, sl+1);
      break;
    default:
      fprintf(stderr, "unknown transport\n");
      goto done;
      break;
  }

  fd = socket(domain, SOCK_STREAM, 0);
  if (fd < 0) {
    fprintf(stderr, "socket: %s\n", strerror(errno));
    goto done;
  }

  sc = connect(fd, sa, sz);
  if (sc < 0) {
    fprintf(stderr, "connect: %s\n", strerror(errno));
    goto done;
  }

  sc = new_epoll(EPOLLIN, fd);
  if (sc < 0) goto done;

  r->fd = fd;
  rc = 0;

 done:
  if ((rc != 0) && (fd != -1)) close(fd);
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
 * if so, return 1 and store its pub* */
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

/* test if fd belongs to an open redis fd.
 * if so, return 1 and store its pub* */
int is_redis(int fd, struct pub **p) {
  int i;

  for(i=0; i < cfg.num_pub; i++) {
    if (cfg.pubv[i].k.fd != fd)
      continue;

    *p = &cfg.pubv[i];
    return 1;
  }

  return 0;
}

/* called when redis is readable */
int handle_redis(struct redis *k, int *ackd) {
  char *err, *r, *t, *eob, *fr;
  ssize_t nr, fl;

  if (ackd) *ackd = 0;

  fr = k->from + k->left;
  fl = BUF_LEN - k->left;
  nr = read(k->fd, fr, fl);
  if (nr <= 0) {
    err = nr ? strerror(errno) : "closed";
    fprintf(stderr, "redis: %s\n", err);
    return -1;
  }

  r = k->from;
  eob = k->from + k->left + nr;
  k->left = 0;

  for (r = k->from; r < eob; r++) {
    t = r;
    if (*t == '\n') continue;
    while ((r < eob) && (*r != '\r')) r++;
    if (r == eob) {
      k->left = eob - t;
      memmove(k->from, t, k->left);
      break;
    }
    switch (*t) {
      case '+': case ':':
        if (ackd) (*ackd)++;
        continue;
      case '-': /* error string */
        err = t + 1;
        fprintf(stderr, "redis: %.*s\n", (int)(r-err), err);
        return -1;
      default:
        fprintf(stderr, "redis: unexpected '%c'\n", *t);
        return -1;
    }
  }


  return 0;
}

/* called when ring is readable */
int handle_ring(struct pub *p) {
  size_t niov, i, l, len;
  char *b, *out, *resp;
  int rc = -1, fl, sc;
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

  p->buf_sent = 0;
  p->buf_used = 0;
  for (i=0; i < niov; i++) {

    b = p->ccr_iov[i].iov_base;
    l = p->ccr_iov[i].iov_len;

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

    /* wrap into redis resp protocol */
    sc = form_resp(&resp, p->k.key, p->k.key_len, out, len);
    if (resp == NULL) goto done;
    out = resp;
    len = sc;

    /* realloc buffer if needed */
    if (p->buf_size - p->buf_used < len) {
      p->buf_size = p->buf_size ? (p->buf_size * 2) : (len * NUM_IOV);
      tmp = realloc(p->out_buf, p->buf_size);
      if (tmp == NULL) {
        fprintf(stderr, "out of memory\n");
        goto done;
      }
      p->out_buf = tmp;
    }

    /* copy working buffer to output buffer */
    memcpy(p->out_buf + p->buf_used, out, len);
    p->buf_used += len;
  }

  /* suspend ring epoll while buffer vents */
  sc = mod_epoll(0, p->fd);
  if (sc < 0) goto done;

  /* vent buffer whenever redis is writable */
  sc = mod_epoll(EPOLLIN|EPOLLOUT, p->k.fd);
  if (sc < 0) goto done;
  p->sent += niov;

  rc = 0;

 done:
  return rc;
}

/*
 * parse_hostport
 *
 * parse <ip|hostname>]:<port>, populate sockaddr_in
 *
 * returns 
 *  0 success
 * -1 error
 *
 */
int parse_hostport() {
  char *colon=NULL, *p, *h;
  struct hostent *e;
  int rc = -1, port;
  struct sockaddr_in *sa;

  sa = &cfg.in;
  memset(sa, 0, sizeof(*sa));

  h = cfg.host;
  colon = strchr(h, ':');
  p = colon ? colon+1 : NULL;
  if (colon) *colon = '\0';
  e = gethostbyname(h);
  if (!e || !e->h_length) {
    fprintf(stderr, "%s: %s\n", h, hstrerror(h_errno));
    goto done;
  }

  port = p ? atoi(p) : DEFAULT_PORT;
  if ((port <= 0) || (port > 65535)) {
    fprintf(stderr, "%s: not a port number\n", p);
    goto done;
  }

  sa->sin_family = AF_INET;
  sa->sin_port = htons(port);
  memcpy(&sa->sin_addr.s_addr, e->h_addr, e->h_length);

  rc = 0;

 done:
  if (colon) *colon = ':';
  return rc;
}

int main(int argc, char *argv[]) {
  int fd, i, sc, opt, ackd, vented;
  char *ring, *key, *colon;
  struct epoll_event ev;
  cfg.prog = argv[0];
  struct ccr *r;
  struct pub *p;
  unsigned n;

  while ( (opt=getopt(argc,argv,"b:Uu:vhjpPV:")) != -1) {
    switch(opt) {
      case 'v': cfg.verbose++; break;
      case 'b': cfg.transport = TRANSPORT_TCP;
                cfg.host = strdup(optarg);
                if (parse_hostport() < 0) goto done;
                break;
      case 'U': cfg.transport = TRANSPORT_UNIX; break;
      case 'u': cfg.unix_socket = strdup(optarg); break;
      case 'V': cfg.verb = strdup(optarg);
                cfg.verb_len = strlen(cfg.verb);
                break;
      case 'j': cfg.json=1; break;
      case 'p': cfg.json=1; cfg.pretty=1; break;
      case 'P': cfg.signal_ppid=1; break;
      case 'h': default: usage(); break;
    }
  }

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
  sc = new_epoll(EPOLLIN, cfg.signal_fd);
  if (sc < 0) goto done;


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
    key = colon ? (colon+1) : ring;
    p->ring_name = ring;
    p->k.key_len = strlen(key);
    p->k.key = key;
    p->k.fd = -1;

    r = ccr_open( ring, CCR_RDONLY|CCR_NONBLOCK);
    if (r == NULL) goto done;
    cfg.pubv[i].ring = r;

    fd = ccr_get_selectable_fd( r );
    if (fd < 0) goto done;
    cfg.pubv[i].fd = fd;

    sc = new_epoll(EPOLLIN, fd);
    if (sc < 0) goto done;

    sc = open_redis(&p->k);
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
    else if (is_redis(ev.data.fd, &p)) {
      if (ev.events & EPOLLIN) {
        sc = handle_redis(&p->k, &ackd);
        if (sc < 0) goto done;
        p->ackd += ackd;
      }
      if (ev.events & EPOLLOUT) {
        sc = send_redis(p, &vented);
        if (sc < 0) goto done;
        if (vented) {
          /* undo pollout on redis */
          sc = mod_epoll(EPOLLIN, p->k.fd);
          if (sc < 0) goto done;
          /* restore pollin on ring */
          sc = mod_epoll(EPOLLIN, p->fd);
          if (sc < 0) goto done;
        }
      }
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
    if (p->k.fd != -1) close(p->k.fd);
    if (p->out_buf) free(p->out_buf);
    /* do not close p->fd */
  }
  if (cfg.pubv) free(cfg.pubv);
  if (cfg.host) free(cfg.host);
  if (cfg.epoll_fd != -1) close(cfg.epoll_fd);
  if (cfg.signal_fd != -1) close(cfg.signal_fd);
  return 0;
}
