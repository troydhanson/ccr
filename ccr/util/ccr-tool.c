#include <sys/signalfd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <netdb.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include "libut.h"
#include "ccr.h"

/* 
 * ccr tool
 *
 */

/* we put a couple of buffers out here 
 * to make them go into bss, whereas if
 * they're in the struct below they'd be
 * in the initialized data area and the
 * resulting binary gets much larger */
#define MAX_FRAME (1024*1024) /* safeguard */
#define SUBBUFLEN (MAX_FRAME * 10)
#define SUBNUMIOV (1024 * 1024)
char sub_buf_bss[SUBBUFLEN];
struct iovec sub_iov_bss[SUBNUMIOV];

struct {
  char *prog;
  int verbose;
  int signal_fd;
  int ticks;
  struct timeval now;
  int epoll_fd;
  char *ring;
  enum {mode_status,
        mode_getfmt,
        mode_create,
        mode_read,
        mode_read_hex,
        mode_pub,
        mode_sub,
        mode_lib} mode;
  struct shr *shr;
  size_t size;
  int flags;
  int fd;
  struct ccr *ccr;
  int block;
  int max;
  int pretty;
  char *file;
  enum {from_unset, from_file, from_ring, from_host} format_from;
  char *format_src;
  /* mode_lib state */
  char *lib;
  char *libopts;
  void *dl;
  int (*libinit)(struct modccr *);
  struct modccr modccr;
  /* pub/sub sub */
  char *addr_spec;
  struct sockaddr_in addr;
  int listen_fd;
  int pub_fd;
  int sub_fd;
  char *sub_buf;
  size_t sub_buf_used;
  struct iovec *sub_iov;
  enum {enc_proto, enc_json, enc_binary} encoding;
  int startup_encoding;
} cfg = {
  .signal_fd = -1,
  .epoll_fd = -1,
  .listen_fd = -1,
  .pub_fd = -1,
  .sub_fd = -1,
  .sub_buf = sub_buf_bss,
  .sub_iov = sub_iov_bss,
  .fd = -1,
};

/* signals that we'll accept via signalfd in epoll */
int sigs[] = {SIGHUP,SIGTERM,SIGINT,SIGQUIT,SIGALRM};

void usage() {
  fprintf(stderr,"usage: %s COMMAND [options] RING\n"
                 "\n"
                 "commands\n"
                 "--------\n"
                 " status          get ring counters\n"
                 " format          get ring format\n"
                 " read            read frames in JSON\n"
                 " readhex         read frames in hex/ascii\n"
                 " create          create a ring\n"
                 " load <mod>      load a module\n"
                 " pub [ip:]port   publish ring over TCP\n"
                 " sub host:port   subscribe to ring pub\n"
                 "\n"
                 "read options\n"
                 "------------\n"
                 "  -b             wait for data when exhausted\n"
                 "  -p             pretty-print\n"
                 "\n"
                 "create options\n"
                 "--------------\n"
                 "  -s size        size with k|m|g|t suffix\n"
                 "  -f file        read format from file\n"
                 "  -C ring        copy format from ring\n"
                 "  -R host:port   fetch format from publisher\n"
                 "  -m dfksl       flags (default: 0)\n"
                 "      d          drop unread frames when full\n"
                 "      f          farm of independent readers\n"
                 "      k          keep ring as-is if it exists\n"
                 "      l          lock into memory when opened\n"
                 "      s          sync after each i/o\n"
                 "\n"
                 "publish options\n"
                 "---------------\n"
                 "  -E j|b|p       publisher mode (default: p)\n"
                 "      j          JSON frames delimited by newlines\n"
                 "      b          binary frames prefixed by 4-byte length\n"
                 "      p          client's g|j|b gets format|JSON|binary\n"
                 "\n"
                 "load options\n"
                 "------------\n"
                 "  -o <args>      pass module parameters\n"
                 "\n"
                 "\n", cfg.prog);
  exit(-1);
}

void hexdump(char *buf, size_t len) {
  size_t i,n=0;
  unsigned char c;
  while(n < len) {
    fprintf(stdout,"%08x ", (int)n);
    for(i=0; i < 16; i++) {
      c = (n+i < len) ? buf[n+i] : 0;
      if (n+i < len) fprintf(stdout,"%.2x ", c);
      else fprintf(stdout, "   ");
    }
    for(i=0; i < 16; i++) {
      c = (n+i < len) ? buf[n+i] : ' ';
      if (c < 0x20 || c > 0x7e) c = '.';
      fprintf(stdout,"%c",c);
    }
    fprintf(stdout,"\n");
    n += 16;
  }
}


int mod_epoll(int events, int fd) {
  struct epoll_event ev;
  int rc = -1, sc;

  memset(&ev, 0, sizeof(ev));
  ev.data.fd = fd;
  ev.events = events;

  sc = epoll_ctl(cfg.epoll_fd, EPOLL_CTL_MOD, fd, &ev);
  if (sc < 0) {
    fprintf(stderr, "epoll_ctl: %s\n", strerror(errno));
    goto done;
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

int handle_signal(void) {
  struct signalfd_siginfo info;
  ssize_t nr;
  int rc=-1;
  
  nr = read(cfg.signal_fd, &info, sizeof(info));
  if (nr != sizeof(info)) {
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

int close_client(void) {
  int rc = -1, sc;

  close(cfg.pub_fd);
  cfg.pub_fd = -1;
  cfg.encoding = cfg.startup_encoding;

  /* stop monitoring ring */
  sc = mod_epoll(0, cfg.fd);
  if (sc < 0) goto done;

  rc = 0;

 done:
  return rc;
}

int handle_io(void) {
  int rc = -1, sc, fl;
  size_t len;
  ssize_t nr;
  char *out;

  switch (cfg.mode) {

    case mode_read:
      fl = CCR_BUFFER | CCR_JSON;
      fl |= cfg.pretty ? CCR_PRETTY : 0;
      sc = ccr_getnext(cfg.ccr, fl, &out, &len);
      if (sc > 0) printf("%.*s\n", (int)len, out);
      if (sc <= 0) {
        fprintf(stderr, "ccr_getnext: %s (%d)\n",
          (sc == 0) ? "end-of-data" : "error", sc);
        goto done;
      }
      break;

    case mode_read_hex:
      fl = CCR_BUFFER;
      sc = ccr_getnext(cfg.ccr, fl, &out, &len);
      if (sc > 0) hexdump(out, len);
      if (sc <= 0) {
        fprintf(stderr, "ccr_getnext: %s (%d)\n",
          (sc == 0) ? "end-of-data" : "error", sc);
        goto done;
      }
      break;

    case mode_lib:
      if (cfg.modccr.mod_work == NULL) goto done;
      if (cfg.modccr.mod_work(&cfg.modccr, cfg.ccr) < 0) {
        fprintf(stderr,"module %s: exit\n", cfg.lib);
        goto done;
      }
      break;

    case mode_pub:
      fl = CCR_BUFFER;
      fl |= (cfg.encoding == enc_json)   ? (CCR_JSON | CCR_NEWLINE) : 0;
      fl |= (cfg.encoding == enc_binary) ? (CCR_LEN4FIRST)          : 0;
      sc = ccr_getnext(cfg.ccr, fl, &out, &len);
      if (sc > 0) do {
        nr = write(cfg.pub_fd, out, len);
        if (nr < 0) {
          fprintf(stderr, "write: %s\n", strerror(errno));
          if (close_client() < 0) goto done;
          break;
        }
        len -= nr;
        out += nr;
      } while (len > 0);

      if (sc < 0) {
        fprintf(stderr, "ccr_getnext: error (%d)\n", sc);
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

/*
 * parse_spec
 *
 * parse [<ip|hostname>]:<port>, populate sockaddr_in
 *
 * if there was no IP/hostname, ip is set to INADDR_ANY
 * port is required, or the function fails
 *
 * returns 
 *  0 success
 * -1 error
 *
 */
int parse_spec(char *spec, struct sockaddr_in *sa) {
  char *colon=NULL, *p, *h;
  struct hostent *e;
  int rc = -1, port;
  uint32_t s_addr;

  memset(sa, 0, sizeof(*sa));

  colon = strchr(spec, ':');
  h = colon ? spec : NULL;
  p = colon ? colon+1 : spec;

  if (colon) *colon = '\0';
  e = h ? gethostbyname(h) : NULL;
  if (h && (!e || !e->h_length)) {
    fprintf(stderr, "%s: %s\n", h, hstrerror(h_errno));
    goto done;
  }

  port = atoi(p);
  if ((port <= 0) || (port > 65535)) {
    fprintf(stderr, "%s: not a port number\n", p);
    goto done;
  }

  sa->sin_family      = AF_INET;
  sa->sin_port        = htons(port);
  sa->sin_addr.s_addr = htonl(INADDR_ANY);
  if (h) memcpy(&sa->sin_addr.s_addr, e->h_addr, e->h_length);

  if (cfg.verbose) {
    fprintf(stderr, "%s -> IP %s port %d\n",
      spec, inet_ntoa(sa->sin_addr), port);
  }

  rc = 0;

 done:
  if (colon) *colon = ':';
  return rc;
}

int proto_sendcast(void) {
  struct shr *shr = NULL;
  char *fmt=NULL, *f;
  int rc = -1, sc;
  size_t fmt_len;
  uint32_t len32;
  ssize_t nr;

  assert(cfg.mode == mode_pub);
  assert(cfg.encoding == enc_proto);

  shr = shr_open(cfg.ring, SHR_RDONLY);
  if (shr == NULL) goto done;

  fmt = NULL;
  fmt_len = 0;
  sc = shr_appdata(shr, (void**)&fmt, NULL, &fmt_len);
  if (sc < 0) {
    fprintf(stderr, "shr_appdata: error %d\n", sc);
    goto done;
  }

  assert(fmt && fmt_len);

  /* send 32-bit length prefix then the cast format */
  len32 = (uint32_t)fmt_len;
  nr = write(cfg.pub_fd, &len32, sizeof(len32));
  if (nr < 0) {
    fprintf(stderr, "write: %s\n", strerror(errno));
    goto done;
  }
  assert (nr == sizeof(len32)); /* TODO drain */
  f = fmt;
  do {
    nr = write(cfg.pub_fd, f, len32);
    if (nr < 0) {
      fprintf(stderr, "write: %s\n", strerror(errno));
      goto done;
    }
    f += nr;
    len32 -= nr;
  } while (len32 > 0);

  rc = 0;

 done:
  if (fmt) free(fmt);
  if (shr) shr_close(shr);
  return rc;
}

/*
 * given a buffer of N frames 
 * with a possible partial final frame
 * find message boundaries and write to ring
 * saving the last frame prefix if partial
 */
int decode_frames(void) {
  char *c, *body, *eob;
  size_t iov_used=0;
  uint32_t blen;
  int rc = -1;
  ssize_t nr;

  assert( cfg.mode == mode_sub );

  eob = cfg.sub_buf + cfg.sub_buf_used;
  c = cfg.sub_buf;
  while(1) {
    if (c + sizeof(uint32_t) > eob) break;
    memcpy(&blen, c, sizeof(uint32_t));
    if (blen > MAX_FRAME) goto done;
    body = c + sizeof(uint32_t);
    if (body + blen > eob) break;
    cfg.sub_iov[ iov_used ].iov_base = body;
    cfg.sub_iov[ iov_used ].iov_len  = blen;
    iov_used++;
    if (iov_used == SUBNUMIOV) break;
    c += sizeof(uint32_t) + blen;
  }

  nr = shr_writev(cfg.shr, cfg.sub_iov, iov_used);
  if (nr < 0) {
    fprintf(stderr,"shr_writev: error (%zd)\n", nr);
    goto done;
  }

  /* if buffer ends with partial frame, save it */
  if (c < eob) memmove(cfg.sub_buf, c, eob - c);
  cfg.sub_buf_used = eob - c;

  rc = 0;

 done:
  if (rc < 0) fprintf(stderr, "frame parsing error\n");
  return rc;
}

/*
 * do_subscriber
 *
 *
 */
int do_subscriber(void) {
  int rc = -1, sc;
  size_t avail;
  ssize_t nr;
  char *b;

  assert( cfg.mode == mode_sub );
  assert( cfg.sub_fd != -1 );

  /* the buffer must have some free space because
   * any time we read data we process it right here,
   * leaving at most a tiny fragment of a partial
   * frame to prepend the next read */
  assert(cfg.sub_buf_used < SUBBUFLEN);
  avail = SUBBUFLEN - cfg.sub_buf_used;
  b = cfg.sub_buf + cfg.sub_buf_used;

  nr = read(cfg.sub_fd, b, avail);
  if (nr <= 0) {
    fprintf(stderr, "read: %s\n", nr ? strerror(errno) : "eof");
    goto done;
  }

  assert(nr > 0);
  cfg.sub_buf_used += nr;
  if (decode_frames() < 0) goto done;

  rc = 0;

 done:
  return rc;
}


/*
 * handle_client
 *
 * in pub mode, client sent data or closed
 *
 */
int handle_client(void) {
  int rc = -1, sc;
  char buf[100], *b;
  ssize_t nr;

  assert( cfg.mode == mode_pub );
  assert( cfg.pub_fd != -1 );

  nr = recv(cfg.pub_fd, buf, sizeof(buf), MSG_DONTWAIT);
  if (nr < 0) {
    fprintf(stderr, "recv: %s\n", strerror(errno) );
    close_client();
    return -1;
  }

  if (nr == 0) {
    fprintf(stderr, "client disconnected\n");
    close_client();
    return 0;
  }

  assert(nr > 0);

  if ((cfg.encoding == enc_binary) ||
      (cfg.encoding == enc_json )) {
    fprintf(stderr, "discarding %zd bytes from client\n", nr);
    return 0;
  }

  assert(cfg.encoding == enc_proto);
  for (b = buf; b < buf+nr; b++) {
    switch (*b) {
      case 'g': 
        sc = proto_sendcast();
        if (sc < 0) {
          close_client();
          return -1;
        }
        break;
      case 'j': 
        cfg.encoding = enc_json;
        sc = mod_epoll(EPOLLIN, cfg.fd); /* start ring monitoring */
        if (sc < 0) return -1;
        break;
      case 'b': 
        cfg.encoding = enc_binary;
        sc = mod_epoll(EPOLLIN, cfg.fd); /* start ring monitoring */
        if (sc < 0) return -1;
        break;
      case 'd': /* disconnect request */
        close_client();
        return 0;
        break;
      case '\n': break;
      default: 
        fprintf(stderr, "unsupported protocol byte\n");
        break;
    }
  }

  return 0;
}

/*
 * accept_client
 *
 * in pub mode, accept client connection
 *
 */
int accept_client(void) {
  struct sockaddr_in remote;
  socklen_t sz = sizeof(remote);
  int rc = -1, sc;

  assert( cfg.mode == mode_pub );

  sc = accept(cfg.listen_fd, (struct sockaddr*)&remote, &sz);
  if (sc < 0) {
    fprintf(stderr, "accept: %s\n", strerror(errno));
    goto done;
  }

  /* one client at a time */
  if (cfg.pub_fd != -1) {
    fprintf(stderr, "refusing secondary client connection\n");
    close(sc);
    rc = 0;
    goto done;
  }

  cfg.pub_fd = sc;
  sc = new_epoll(EPOLLIN, cfg.pub_fd);
  fprintf(stderr, "connection from %s\n", inet_ntoa(remote.sin_addr));

  /* monitor the ring. except enc_proto waits for client */
  if (cfg.encoding != enc_proto) {
    sc = mod_epoll(EPOLLIN, cfg.fd);
    if (sc < 0) goto done;
  }

  rc = 0;

 done:
  return rc;
}

/*
 * setup_subscriber
 *
 *
 */
int setup_subscriber(void) {
  int rc = -1, sc;
  ssize_t nr;

  cfg.sub_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (cfg.sub_fd == -1) {
    fprintf(stderr, "socket: %s\n", strerror(errno));
    goto done;
  }

  sc = parse_spec(cfg.addr_spec, &cfg.addr);
  if (sc < 0) goto done;

  sc = connect(cfg.sub_fd, (struct sockaddr*)&cfg.addr, sizeof(cfg.addr));
  if (sc < 0) {
    fprintf(stderr, "connect: %s\n", strerror(errno));
    goto done;
  }

  /* for now we induce binary publishing mode with
   * no attempt to validate the ring compatibility */
  nr = write(cfg.sub_fd, "b", 1);
  if (nr < 0) {
    fprintf(stderr, "write: %s\n", strerror(errno));
    goto done;
  }

  rc = 0;

 done:
  return rc;
}

/*
 * setup_listener
 *
 * bring up listening socket for publishing
 *
 */
int setup_listener(void) {
  int rc = -1, sc, one=1;

  cfg.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (cfg.listen_fd == -1) {
    fprintf(stderr, "socket: %s\n", strerror(errno));
    goto done;
  }

  sc = parse_spec(cfg.addr_spec, &cfg.addr);
  if (sc < 0) goto done;

  sc = setsockopt(cfg.listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  if (sc < 0) {
    fprintf(stderr, "setsockopt: %s\n", strerror(errno));
    goto done;
  }

  sc = bind(cfg.listen_fd, (struct sockaddr*)&cfg.addr, sizeof(cfg.addr));
  if (sc < 0) {
    fprintf(stderr, "bind: %s\n", strerror(errno));
    goto done;
  }

  sc = listen(cfg.listen_fd, 1);
  if (sc < 0) {
    fprintf(stderr, "listen: %s\n", strerror(errno));
    goto done;
  }

  rc = 0;

 done:
  return rc;
}

/*
 * fetch_cast
 *
 * get cast format from remote publisher 
 * spec is host:port
 * on success, cast is allocated and castlen its size
 * caller must free cast when done
 *
 * returns
 *  0 success
 * -1 error
 */
int fetch_cast(char *spec, char **cast, size_t *castlen) {
  struct sockaddr_in addr;
  int rc = -1, sc, fd = -1;
  char *buf = NULL;
  ssize_t nr, buflen;

  *cast = NULL;
  *castlen = 0;

  buflen = 8192; /* TODO realloc not fixed */
  buf = malloc(buflen);
  if (buf == NULL) {
    fprintf(stderr, "out of memory\n");
    goto done;
  }

  sc = parse_spec(spec, &addr);
  if (sc < 0) goto done;

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    fprintf(stderr, "socket: %s\n", strerror(errno));
    goto done;
  }

  sc = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
  if (sc < 0) {
    fprintf(stderr, "connect: %s\n", strerror(errno));
    goto done;
  }

  /* (g)etcast and (d)isconnect */
  nr = write(fd, "gd", 2);
  if (nr != 2) {
    fprintf(stderr, "write: %s\n", (nr < 0) ? 
       strerror(errno) : "incomplete");
    goto done;
  }

  /* drain reply */
  do {
    if (buflen == *castlen) {
      fprintf(stderr, "cast too long\n");
      goto done;
    }
    nr = read(fd, buf+(*castlen), buflen-(*castlen));
    if (nr < 0) {
      fprintf(stderr, "read: %s\n", strerror(errno));
      goto done;
    }
    *castlen += nr;
  } while (nr > 0);

  /* elide length prefix from cast */
  if (*castlen <= sizeof(uint32_t)) {
    fprintf(stderr, "cast too short\n");
    goto done;
  }

  memmove(buf, buf+sizeof(uint32_t), (*castlen) - sizeof(uint32_t));
  *castlen -= sizeof(uint32_t);
  *cast = buf;

  rc = 0;
 
 done:
  if (rc && buf) free(buf);
  if (fd != -1) close(fd);
  return rc;
}

int main(int argc, char *argv[]) {
  int opt, rc=-1, sc, n, ec, open_mode=0, tmo, 
    one_shot=0, epoll_mode;
  char unit, *c, *fmt, *out, *cmd;
  struct epoll_event ev;
  struct shr_stat stat;
  size_t fmt_len, len;
  cfg.prog = argv[0];

  if (argc < 3) usage();

  cmd = argv[1];
  if      (!strcmp(cmd, "status"))    cfg.mode = mode_status;
  else if (!strcmp(cmd, "format"))    cfg.mode = mode_getfmt;
  else if (!strcmp(cmd, "read"))      cfg.mode = mode_read;
  else if (!strcmp(cmd, "readhex"))   cfg.mode = mode_read_hex;
  else if (!strcmp(cmd, "create"))    cfg.mode = mode_create;
  else if (!strcmp(cmd, "load"))      cfg.mode = mode_lib;
  else if (!strcmp(cmd, "pub"))       cfg.mode = mode_pub;
  else if (!strcmp(cmd, "sub"))       cfg.mode = mode_sub;
  else /* "help" or anything else */  usage();

  argv++;
  argc--;

  if (cfg.mode == mode_lib) {
      if (argc < 2) usage();
      cfg.lib = strdup(argv[1]);
      argv++;
      argc--;
  }

  if ((cfg.mode == mode_pub) || (cfg.mode == mode_sub)) {
      if (argc < 2) usage();
      cfg.addr_spec=strdup(argv[1]);
      argv++;
      argc--;
  }

  while ( (opt = getopt(argc,argv,"vs:m:bf:po:C:E:R:")) > 0) {
    switch(opt) {
      default : usage(); break;
      case 'v': cfg.verbose++; break;
      case 'p': cfg.pretty++; break;
      case 'b': cfg.block = 1; break;
      case 'f':
      case 'C':
      case 'R':
        if (cfg.format_from != from_unset) {
          fprintf(stderr, "only one of -f|-C|-R may be used\n");
          exit(-1);
        }
        if (opt == 'f') cfg.format_from=from_file;
        if (opt == 'C') cfg.format_from=from_ring;
        if (opt == 'R') cfg.format_from=from_host;
        cfg.format_src=strdup(optarg);
        break;
      case 'E': switch (*optarg) {
                  case 'j': cfg.encoding = enc_json; break;
                  case 'b': cfg.encoding = enc_binary; break;
                  case 'p': cfg.encoding = enc_proto; break;
                  default : usage(); break;
                }
                cfg.startup_encoding = cfg.encoding;
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
         c = optarg;
         while((*c) != '\0') {
           switch (*c) {
             case 'd': cfg.flags |= CCR_DROP; break;
             case 'f': cfg.flags |= CCR_FARM; break;
             case 'k': cfg.flags |= CCR_KEEPEXIST; break;
             case 'l': cfg.flags |= CCR_MLOCK; break;
             case 's': cfg.flags |= CCR_SYNC; break;
             default: usage(); break;
           }
           c++;
         }
         break;
    }
  }

  if (optind >= argc) usage();
  cfg.ring = argv[optind++];

  switch(cfg.mode) {

    case mode_create:
      if (cfg.size == 0) usage();
      if (cfg.format_src == NULL) usage();
      if (cfg.format_from == from_host) {
        cfg.flags |= CCR_CASTTEXT;
        char *cast;
        size_t castlen;
        sc = fetch_cast(cfg.format_src, &cast, &castlen);
        if (sc < 0) goto done;
        assert(cast && castlen);
        rc = ccr_init(cfg.ring, cfg.size, cfg.flags, cast, castlen);
        if (rc < 0) goto done;
        free(cast);
      }
      if (cfg.format_from == from_file) {
        cfg.flags |= CCR_CASTFILE;
        rc = ccr_init(cfg.ring, cfg.size, cfg.flags, cfg.format_src);
        if (rc < 0) goto done;
      }
      if (cfg.format_from == from_ring) {
        cfg.flags |= CCR_CASTCOPY;
        rc = ccr_init(cfg.ring, cfg.size, cfg.flags, cfg.format_src);
        if (rc < 0) goto done;
      }
      one_shot=1;
      break;

    case mode_status:
    case mode_getfmt:
      cfg.shr = shr_open(cfg.ring, SHR_RDONLY);
      if (cfg.shr == NULL) goto done;
      rc = shr_stat(cfg.shr, &stat, NULL);
      if (rc < 0) goto done;
      if (cfg.mode == mode_status) {
        printf(" frames-written %ld\n"
               " frames-read %ld\n"
               " frames-dropped %ld\n"
               " ring-size %ld\n"
               " frames-ready %ld\n",
             stat.mw, stat.mr, stat.md, stat.bn, stat.mu);
        printf(" attributes ");
        if (stat.flags == 0)          printf("none");
        if (stat.flags & SHR_DROP)    printf("drop ");
        if (stat.flags & SHR_APPDATA) printf("appdata ");
        if (stat.flags & SHR_FARM)    printf("farm ");
        if (stat.flags & SHR_MLOCK)   printf("mlock ");
        if (stat.flags & SHR_SYNC)    printf("sync ");
        printf("\n");
      }

      if (cfg.mode == mode_getfmt) {
        fmt = NULL;
        fmt_len = 0;
        rc = shr_appdata(cfg.shr, (void**)&fmt, NULL, &fmt_len);
        if (rc < 0) {
          fprintf(stderr, "shr_appdata: error %d\n", rc);
          goto done;
        }
        assert(fmt && fmt_len);
        printf("%.*s", (int)fmt_len, fmt);
        free(fmt);
      }
      one_shot=1;
      break;

    case mode_pub:
      sc = setup_listener();
      if (sc < 0) goto done;
      /* FALL THROUGH */
    case mode_lib:
    case mode_read:
    case mode_read_hex:
      open_mode = CCR_RDONLY | CCR_NONBLOCK;
      cfg.ccr = ccr_open(cfg.ring, open_mode, 0);
      if (cfg.ccr == NULL) goto done;
      cfg.fd = ccr_get_selectable_fd(cfg.ccr);
      if (cfg.fd < 0) goto done;
      break;

    case mode_sub:
      cfg.shr = shr_open(cfg.ring, SHR_WRONLY);
      if (cfg.shr == NULL) goto done;
      sc = setup_subscriber();
      if (sc < 0) goto done;
      break;

    default: 
      assert(0);
      break;
  }

  if (one_shot) {
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
  sc = new_epoll(EPOLLIN, cfg.signal_fd);
  if (sc < 0) goto done;

  if (cfg.mode == mode_pub) {
    sc = new_epoll(EPOLLIN, cfg.listen_fd);
    if (sc < 0) goto done;
  }

  if (cfg.mode == mode_sub) {
    sc = new_epoll(EPOLLIN, cfg.sub_fd);
    if (sc < 0) goto done;
  }

  /* most modes poll the ring immediately */
  epoll_mode = (cfg.mode == mode_pub) ? 0 : EPOLLIN;

  switch (cfg.mode) {
    case mode_lib:
    case mode_read:
    case mode_read_hex:
      sc = new_epoll(EPOLLIN, cfg.fd);
      if (sc < 0) goto done;
      break;
    case mode_pub:
      /* poll when connected */
      sc = new_epoll(0, cfg.fd);
      if (sc < 0) goto done;
      break;
    default:
      /* no ring poll in these modes */
      assert(cfg.fd == -1);
      break;
  }


  /* load module, if any */
  if (load_module() < 0) goto done;

  /* kick off timer */
  alarm(1);

  /* induce an immediate timeout in non-blocking read mode */
  tmo = ((cfg.block == 0) && 
         ((cfg.mode == mode_read) || (cfg.mode == mode_read_hex))) ? 0 : -1;

  do { 
    ec = epoll_wait(cfg.epoll_fd, &ev, 1, tmo);
    if      (ec < 0)  fprintf(stderr, "epoll: %s\n", strerror(errno));
    else if (ec == 0) /* print no-data */ { if (handle_io()     < 0) goto done;}
    else if (ev.data.fd == cfg.fd)        { if (handle_io()     < 0) goto done;}
    else if (ev.data.fd == cfg.signal_fd) { if (handle_signal() < 0) goto done;}
    else if (ev.data.fd == cfg.listen_fd) { if (accept_client() < 0) goto done;}
    else if (ev.data.fd == cfg.pub_fd)    { if (handle_client() < 0) goto done;}
    else if (ev.data.fd == cfg.sub_fd)    { if (do_subscriber() < 0) goto done;}
    else { assert(0); }
  } while (ec >= 0);

  rc = 0;
 
 done:
  if (cfg.signal_fd != -1) close(cfg.signal_fd);
  if (cfg.listen_fd != -1) close(cfg.listen_fd);
  if (cfg.pub_fd != -1) close(cfg.pub_fd);
  if (cfg.sub_fd != -1) close(cfg.sub_fd);
  if (cfg.epoll_fd != -1) close(cfg.epoll_fd);
  if (cfg.format_src) free(cfg.format_src);
  /* module cleanup */
  if (cfg.dl && cfg.modccr.mod_fini) cfg.modccr.mod_fini(&cfg.modccr);
  if (cfg.lib) free(cfg.lib);
  if (cfg.libopts) free(cfg.libopts);
  if (cfg.dl) dlclose(cfg.dl);

  if (cfg.shr) shr_close(cfg.shr);
  if (cfg.ccr) ccr_close(cfg.ccr);
  /* don't close cfg.fd - it's done in ccr_close */
  if (cfg.addr_spec) free(cfg.addr_spec);
  return 0;
}
