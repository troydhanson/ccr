#include <sys/signalfd.h>
#include <sys/epoll.h>
#include <termios.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include "libut.h"
#include "ccr.h"
#include "modccr.h"

/* 
 * ccr test tool
 *
 */

struct {
  char *prog;
  int verbose;
  int signal_fd;
  int ticks;
  struct timeval now;
  int epoll_fd;
  char *ring;
  enum {mode_status, mode_getcast, mode_create, mode_read, mode_lib} mode;
  struct shr *shr;
  size_t size;
  int flags;
  UT_vector /* of int */ *fd;
  UT_vector /* of struct ccr* */ *ccr;
  int block;
  int max;
  int pretty;
  char *file;
  /* mode_lib state */
  char *lib;
  char *libopts;
  void *dl;
  int (*libinit)(struct modccr *);
  struct modccr modccr;
} cfg = {
  .signal_fd = -1,
  .epoll_fd = -1,
  .flags = CCR_KEEPEXIST | CCR_DROP,
  .block = 1,
};

/* signals that we'll accept via signalfd in epoll */
int sigs[] = {SIGHUP,SIGTERM,SIGINT,SIGQUIT,SIGALRM};

int new_epoll(int events, int fd) {
  int rc;
  struct epoll_event ev;
  memset(&ev,0,sizeof(ev)); // placate valgrind
  ev.events = events;
  ev.data.fd= fd;
  if (cfg.verbose) fprintf(stderr,"adding fd %d to epoll\n", fd);
  rc = epoll_ctl(cfg.epoll_fd, EPOLL_CTL_ADD, fd, &ev);
  if (rc == -1) {
    fprintf(stderr,"epoll_ctl: %s\n", strerror(errno));
  }
  return rc;
}

int handle_signal(void) {
  int rc=-1;
  struct signalfd_siginfo info;
  
  if (read(cfg.signal_fd, &info, sizeof(info)) != sizeof(info)) {
    fprintf(stderr,"failed to read signal fd buffer\n");
    goto done;
  }

  switch(info.ssi_signo) {
    case SIGALRM: 
      cfg.ticks++;
      gettimeofday(&cfg.now, NULL);

      /* in module mode, run the module's periodic function */
      if ((cfg.mode == mode_lib) && cfg.modccr.mod_periodic) {
        if (cfg.modccr.mod_periodic(&cfg.modccr) < 0) {
          fprintf(stderr, "module %s: shutdown\n", cfg.lib);
          goto done;
        }
      }

      alarm(1); 
      break;
    default: 
      fprintf(stderr,"got signal %d\n", info.ssi_signo);  
      goto done;
      break;
  }

 rc = 0;

 done:
  return rc;
}

/* this toggles terminal settings so we read keystrokes on
 * stdin immediately (by negating canon and echo flags). 
 *
 * Undo at exit, or user's terminal will appear dead!!
 */
int want_keys(int want_keystrokes) {
  int rc = -1;
  struct termios t;

  if (isatty(STDIN_FILENO) == 0) return 0;

  if (tcgetattr(STDIN_FILENO, &t) < 0) {
    fprintf(stderr,"tcgetattr: %s\n", strerror(errno));
    goto done;
  }

  if (want_keystrokes) t.c_lflag &= ~(ICANON|ECHO);
  else                 t.c_lflag |=  (ICANON|ECHO);

  if (tcsetattr(STDIN_FILENO, TCSANOW, &t) < 0) {
    fprintf(stderr,"tcsetattr: %s\n", strerror(errno));
    goto done;
  }
  rc = 0;
 done:
  return rc;
}

int handle_stdin(void) {
  int rc= -1, bc;
  char c;

  bc = read(STDIN_FILENO, &c, sizeof(c));
  if (bc <= 0) goto done;

  if (c == 'q') goto done; /* quit */ 
  else          goto done; /* right now, any key quits */
  rc = 0;

 done:
  return rc;
}

int handle_io(int i) {
  int rc = -1, sc, fl;
  size_t len;
  char *out;

  struct ccr **ccr = utvector_elt(cfg.ccr, i);
  assert(ccr);

  switch (cfg.mode) {

    case mode_read:
      fl = CCR_BUFFER | CCR_JSON;
      fl |= cfg.pretty ? CCR_PRETTY : 0;
      sc = ccr_getnext(*ccr, fl, &out, &len);
      if (sc < 0) goto done;
      if (sc > 0) printf("%.*s\n", (int)len, out);
      break;

    case mode_lib:
      if (cfg.modccr.mod_work == NULL) goto done;
      if (cfg.modccr.mod_work(&cfg.modccr, *ccr) < 0) {
        fprintf(stderr,"module %s: exit\n", cfg.lib);
        goto done;
      }
      break;

    default:
      assert(0);
      goto done;
      break;
  }

  rc = 0;

 done:
  return rc;
}

void usage() {
  fprintf(stderr,"usage: %s [options] <ring>\n"
                 "\n"
                 "[query stats ]: -q\n"
                 "[get cast    ]: -g\n"
                 "[read frames ]: -r\n"
                 "[load module ]: -l <module> [-o <module-options>]\n"
                 "[create/init ]: -c -s <size> -f <cast-file> [-m <mode>]\n"
                 "\n"
                 "read mode options\n"
                 "-----------------\n"
                 "  -b [0|1]   block (default: 1; wait for data)\n"
                 "  -n <num>   max frames to read (default: 0; unlimited)\n"
                 "  -p         pretty-print JSON (default: 0)\n"
                 "\n"
                 "create mode options\n"
                 "-------------------\n"
                 "  <size> in bytes with optional k/m/g/t suffix\n"
                 "  <cast-file> is the format of the items in ccr castfile format\n"
                 "  <mode> bits (default: dk)\n"
                 "         d  drop mode     (drop unread data when full)\n"
                 "         k  keep existing (if ring exists, leave as-is)\n"
                 "         o  overwrite     (if ring exists, re-create)\n"
                 "\n"
                 "\n", cfg.prog);
  exit(-1);
}

int load_module(void) {
  int rc = -1;

  if (cfg.mode != mode_lib) {
    rc = 0;
    goto done;
  }
    
  cfg.dl = dlopen(cfg.lib, RTLD_LAZY);
  if (cfg.dl == NULL) {
    fprintf(stderr, "%s\n", dlerror());
    goto done;
  }

  cfg.libinit = dlsym(cfg.dl, "ccr_module_init");
  if (cfg.libinit == NULL) {
    fprintf(stderr, "%s\n", dlerror());
    goto done;
  }

  cfg.modccr.verbose = cfg.verbose;
  cfg.modccr.opts = cfg.libopts;

  if (cfg.libinit(&cfg.modccr) < 0) {
    fprintf(stderr, "%s: module init failed\n", cfg.lib);
    goto done;
  }

  rc = 0;

 done:
  return rc;
}

int is_ccr_fd(int fd, int *i) {
  int *fp;
  *i = 0;
  fp=NULL;
  while ( (fp = (int*)utvector_next(cfg.fd, fp))) {
    if (*fp == fd) return 1;
    (*i)++;
  }
  return 0;
}

int main(int argc, char *argv[]) {
  int opt, rc=-1, sc, n, ec, fd, i, *fp;
  struct ccr *ccr = NULL, **r;
  char unit, *c, *cast, *out;
  struct epoll_event ev;
  size_t cast_len, len;
  struct shr_stat stat;
  cfg.prog = argv[0];

  UT_mm mm_ptr = {.sz = sizeof(void*) };
  cfg.fd = utvector_new(utmm_int);
  cfg.ccr = utvector_new(&mm_ptr);

  while ( (opt = getopt(argc,argv,"vhcs:qm:rb:f:gn:pl:o:")) > 0) {
    switch(opt) {
      case 'v': cfg.verbose++; break;
      case 'p': cfg.pretty++; break;
      case 'h': default: usage(); break;
      case 'b': cfg.block = atoi(optarg); break;
      case 'n': cfg.max = atoi(optarg); break;
      case 'c': cfg.mode=mode_create; break;
      case 'q': cfg.mode=mode_status; break;
      case 'g': cfg.mode=mode_getcast; break;
      case 'r': cfg.mode=mode_read; break;
      case 'f': cfg.file = strdup(optarg); break;
      case 'l': cfg.mode = mode_lib;
                cfg.lib = strdup(optarg);
                break;
      case 'o': cfg.libopts = strdup(optarg); break;
      case 's':  /* ring size */
         sc = sscanf(optarg, "%ld%c", &cfg.size, &unit);
         if (sc == 0) usage();
         if (sc == 2) {
            switch (unit) {
              case 't': case 'T': cfg.size *= 1024; /* fall through */
              case 'g': case 'G': cfg.size *= 1024; /* fall through */
              case 'm': case 'M': cfg.size *= 1024; /* fall through */
              case 'k': case 'K': cfg.size *= 1024; break;
              default: usage(); break;
            }
         }
         break;
      case 'm': /* ring mode */
         cfg.flags = 0; /* override default */
         c = optarg;
         while((*c) != '\0') {
           switch (*c) {
             case 'd': cfg.flags |= CCR_DROP; break;
             case 'k': cfg.flags |= CCR_KEEPEXIST; break;
             case 'o': cfg.flags |= CCR_OVERWRITE; break;
             default: usage(); break;
           }
           c++;
         }
         break;
    }
  }

  if (optind >= argc) usage();

  while(optind < argc) {
    cfg.ring = argv[optind++];
  
    switch(cfg.mode) {

      case mode_create:
        if (cfg.size == 0) usage();
        if (cfg.file == NULL) usage();
        rc = ccr_init(cfg.ring, cfg.size, cfg.flags | CCR_CASTFILE, cfg.file);
        if (rc < 0) goto done;
        break;

      case mode_status:
      case mode_getcast:
        cfg.shr = shr_open(cfg.ring, SHR_RDONLY | SHR_GET_APPDATA, 
                          &cast, &cast_len);
        if (cfg.shr == NULL) goto done;
        rc = shr_stat(cfg.shr, &stat, NULL);
        if (rc < 0) goto done;
        if (cfg.mode == mode_status) {
          printf("%s, frames-ready %ld,"
               " frames-written %ld,"
               " frames-read %ld,"
               " frames-dropped %ld,"
               " byte-capacity %ld\n",
               cfg.ring, stat.mu, stat.mw, stat.mr, stat.md, stat.bn);
        }
        if (cfg.mode == mode_getcast) {
          printf("%.*s", (int)cast_len, cast);
        }
        shr_close(cfg.shr);
        break;

      case mode_read:
        /* unbuffer keypresses from terminal */
        if (want_keys(1) < 0) goto done;
        /* FALLTHRU */
      case mode_lib:
        ccr = ccr_open(cfg.ring, CCR_RDONLY|CCR_NONBLOCK, 0);
        if (ccr == NULL) goto done;
        utvector_push(cfg.ccr, &ccr);
        fd = ccr_get_selectable_fd(ccr);
        if (fd < 0) goto done;
        utvector_push(cfg.fd, &fd);
        setvbuf(stdout, NULL, _IONBF, 0);
        break;

      default: 
        assert(0);
        break;
    }
  }

  /* one-shot modes are done. */
  if (utvector_len(cfg.fd) == 0) {
    rc = 0;
    goto done;
  }

  /* block signals. we accept signals in signalfd */
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
  if (new_epoll(EPOLLIN, cfg.signal_fd)) goto done;
  if ((cfg.mode == mode_read) && isatty(STDIN_FILENO)) {
    if (new_epoll(EPOLLIN, STDIN_FILENO)) goto done;
  }
  fp=NULL;
  while ( (fp = (int*)utvector_next(cfg.fd, fp))) {
    if (new_epoll(EPOLLIN, *fp)) goto done;
  }

  /* load module, if any */
  if (load_module() < 0) goto done;

  /* kick off timer */
  alarm(1);

  do { 
    ec = epoll_wait(cfg.epoll_fd, &ev, 1, -1);
    if      (ec < 0)  fprintf(stderr, "epoll: %s\n", strerror(errno));
    else if (ec == 0) { assert(0); }
    else if (ev.data.fd == cfg.signal_fd) { if (handle_signal() < 0) goto done;}
    else if (ev.data.fd == STDIN_FILENO)  { if (handle_stdin()  < 0) goto done;}
    else if (is_ccr_fd(ev.data.fd, &i))   { if (handle_io(i)    < 0) goto done;}
    else { assert(0); }
  } while (ec >= 0);


  rc = 0;
 
 done:
  if (cfg.signal_fd != -1) close(cfg.signal_fd);
  if (cfg.epoll_fd != -1) close(cfg.epoll_fd);
  if (cfg.file) free(cfg.file);
  /* module cleanup */
  if (cfg.dl && cfg.modccr.mod_fini) cfg.modccr.mod_fini(&cfg.modccr);
  if (cfg.lib) free(cfg.lib);
  if (cfg.libopts) free(cfg.libopts);
  if (cfg.dl) dlclose(cfg.dl);
  /* ring cleanup */
  r=NULL;
  while ( (r = (struct ccr**)utvector_next(cfg.ccr, r))) ccr_close(*r);
  utvector_free(cfg.ccr);
  utvector_free(cfg.fd); /* each fd closed in ccr_close */
  want_keys(0); /* restore terminal */
  return 0;
}
