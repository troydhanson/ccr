/*
 * example of reading ccr ring in batches
 */

#include <sys/signalfd.h>
#include <sys/epoll.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "ccr.h"

#define NUM_IOV 100000
#define BUF_LEN (NUM_IOV * 1000)
struct iovec iov_bss[NUM_IOV];
char buf_bss[BUF_LEN];

struct {
  int verbose;
  char *prog;
  int signal_fd;
  int epoll_fd;
  int ticks;
  int json;
  int pretty_json;
  struct iovec *iov;
  char *buf;
  int num_rings;
  struct ccr **ringv;
  int *ring_fdv;
} cfg = {
  .signal_fd = -1,
  .epoll_fd = -1,
  .iov = iov_bss,
  .buf = buf_bss,
};

void usage() {
  fprintf(stderr,"usage: %s [options] <ring> ...\n", cfg.prog);
  fprintf(stderr,"\n");
  fprintf(stderr,"  -v               verbose\n");
  fprintf(stderr,"  -j               json\n");
  fprintf(stderr,"  -p               pretty json\n");
  fprintf(stderr,"  -h               this help\n");
  fprintf(stderr,"\n");
  exit(-1);
}

/* signals that we'll accept via signalfd in epoll */
int sigs[] = {SIGHUP,SIGTERM,SIGINT,SIGQUIT,SIGALRM};

void periodic_work() {
  fprintf(stderr,"periodic work...\n");
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

int handle_signal() {
  struct signalfd_siginfo info;
  ssize_t nr;
  int rc=-1;
  char *s;
  
  nr = read(cfg.signal_fd, &info, sizeof(info));
  if (nr != sizeof(info)) {
    fprintf(stderr,"failed to read signal fd buffer\n");
    goto done;
  }

  switch(info.ssi_signo) {
    case SIGALRM: 
      if ((++cfg.ticks % 2) == 0) periodic_work();
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
int is_ring(int fd, struct ccr **r) {
  int i;

  for(i=0; i < cfg.num_rings; i++) {
    if (cfg.ring_fdv[i] != fd)
      continue;

    *r = cfg.ringv[i];
    return 1;
  }

  return 0;
}

/* called when ring is readable */
int handle_ring(struct ccr *r) {
  size_t niov, i, l, len;
  int rc = -1, fl, sc;
  char *b, *out;
  ssize_t nr;

  niov = NUM_IOV;
  nr = ccr_readv(r, 0, cfg.buf, BUF_LEN, cfg.iov, &niov);
  if (nr < 0) {
    fprintf(stderr, "ccr_readv: error %zd\n", nr);
    goto done;
  }
  if (nr == 0) { /* spurious wakeup; ignore */
    rc = 0;
    goto done;
  }

  if (cfg.verbose) {
    fprintf(stderr, "read %zu frames (%zd bytes)\n", niov, nr);
  }

  struct cc *cc;
  cc = ccr_get_cc( r );
  fl = 0;
  fl |= cfg.json ? CCR_JSON : 0;
  fl |= cfg.pretty_json ? CCR_PRETTY : 0;

  for (i=0; i < niov; i++) {

    b = cfg.iov[i].iov_base;
    l = cfg.iov[i].iov_len;

    if (cfg.verbose) {
      fprintf(stderr, "iov %zu: length %zu\n", i, l);
    }

    /* print out the buffer */
    if (cfg.json) {
      sc = cc_to_json(cc, &out, &len, b, l, fl);
      if (sc < 0) {
        fprintf(stderr, "json conversion failed\n");
        goto done;
      }
      fprintf(stderr, "%.*s\n", (int)len, out);
    }
  }

  rc = 0;

 done:
  return rc;
}

int main(int argc, char *argv[]) {
  struct epoll_event ev;
  cfg.prog = argv[0];
  int fd, i, sc, opt;
  struct ccr *r;
  unsigned n;

  while ( (opt=getopt(argc,argv,"vhjp")) != -1) {
    switch(opt) {
      case 'v': cfg.verbose++; break;
      case 'j': cfg.json=1; break;
      case 'p': cfg.pretty_json=1; break;
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
  if (new_epoll(EPOLLIN, cfg.signal_fd))   goto done;

  /* rings from command line */
  cfg.num_rings = argc - optind;
  if (cfg.num_rings == 0) usage();

  cfg.ringv = calloc(cfg.num_rings, sizeof(struct ccr*));
  if (cfg.ringv == NULL) goto done;
  cfg.ring_fdv = calloc(cfg.num_rings, sizeof(int));
  if (cfg.ring_fdv == NULL) goto done;

  for(i=0; optind < argc; i++, optind++) {
    if (cfg.verbose)
      fprintf(stderr, "opening %s\n", argv[optind]);
    r = ccr_open( argv[optind], CCR_RDONLY|CCR_NONBLOCK);
    if (r == NULL) goto done;
    cfg.ringv[i] = r;

    fd = ccr_get_selectable_fd( r );
    if (fd < 0) goto done;
    cfg.ring_fdv[i] = fd;

    sc = new_epoll(EPOLLIN, fd);
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
    } else if (is_ring(ev.data.fd, &r)) {
      sc = handle_ring(r);
      if (sc < 0) goto done;
    } else {
      fprintf(stderr, "unknown fd\n");
      assert(0);
    }

  }

done:
  for(i=0; i < cfg.num_rings; i++) {
    assert(cfg.ringv);
    r = cfg.ringv[ i ];
    if (r == NULL) break;
    ccr_close( r );
    /* do not close cfg.ring_fdv[i] */
  }
  if (cfg.ringv) free(cfg.ringv);
  if (cfg.ring_fdv) free(cfg.ring_fdv);
  if (cfg.epoll_fd != -1) close(cfg.epoll_fd);
  if (cfg.signal_fd != -1) close(cfg.signal_fd);
  return 0;
}
