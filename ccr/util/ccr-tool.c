#include <string.h>
#include <time.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include "ccr.h"

/* 
 * ccr test tool
 *
 */

struct {
  char *prog;
  int verbose;
  char *ring;
  enum {mode_status, mode_getcast, mode_create, mode_read} mode;
  struct ccr *ccr;
  struct shr *shr;
  size_t size;
  int flags;
  int block;
  char *file;
} CF = {
  .flags = CCR_KEEPEXIST | CCR_DROP,
};

void usage() {
  fprintf(stderr,"usage: %s [options] <ring>\n"
                 "\n"
                 "[query stats ]: -q\n"
                 "[get cast    ]: -g\n"
                 "[read frame  ]: -r\n"
                 "[create/init ]: -c -s <size> -f <cast-file> [-m <mode>]\n"
                 "\n"
                 "create mode options\n"
                 "-------------------\n"
                 "\n"
                 "  <size> in bytes with optional k/m/g/t suffix\n"
                 "  <cast-file> is the format of the items in ccr castfile format\n"
                 "  <mode> bits (default: dk)\n"
                 "         d  drop mode     (drop unread data when full)\n"
                 "         k  keep existing (if ring exists, leave as-is)\n"
                 "         o  overwrite     (if ring exists, re-create)\n"
                 "\n"
                 "\n", CF.prog);
  exit(-1);
}

int main(int argc, char *argv[]) {
  char unit, *c, buf[10000], *cast, *out;
  struct ccr *ccr = NULL;
  size_t cast_len, len;
  struct shr_stat stat;
  int opt, rc=-1, sc;
  CF.prog = argv[0];

  while ( (opt = getopt(argc,argv,"vhcs:qm:rb:f:g")) > 0) {
    switch(opt) {
      case 'v': CF.verbose++; break;
      case 'h': default: usage(); break;
      case 'b': CF.block = atoi(optarg); break;
      case 'c': CF.mode=mode_create; break;
      case 'q': CF.mode=mode_status; break;
      case 'g': CF.mode=mode_getcast; break;
      case 'r': CF.mode=mode_read; break;
      case 'f': CF.file = strdup(optarg); break;
      case 's':  /* ring size */
         sc = sscanf(optarg, "%ld%c", &CF.size, &unit);
         if (sc == 0) usage();
         if (sc == 2) {
            switch (unit) {
              case 't': case 'T': CF.size *= 1024; /* fall through */
              case 'g': case 'G': CF.size *= 1024; /* fall through */
              case 'm': case 'M': CF.size *= 1024; /* fall through */
              case 'k': case 'K': CF.size *= 1024; break;
              default: usage(); break;
            }
         }
         break;
      case 'm': /* ring mode */
         CF.flags = 0; /* override default */
         c = optarg;
         while((*c) != '\0') {
           switch (*c) {
             case 'd': CF.flags |= CCR_DROP; break;
             case 'k': CF.flags |= CCR_KEEPEXIST; break;
             case 'o': CF.flags |= CCR_OVERWRITE; break;
             default: usage(); break;
           }
           c++;
         }
         break;
    }
  }

  if (optind >= argc) usage();

  while(optind < argc) {
    CF.ring = argv[optind++];
  
    switch(CF.mode) {

      case mode_create:
        if (CF.size == 0) usage();
        if (CF.file == NULL) usage();
        rc = ccr_init(CF.ring, CF.size, CF.flags | CCR_CASTFILE, CF.file);
        if (rc < 0) goto done;
        break;

      case mode_status:
      case mode_getcast:
        CF.shr = shr_open(CF.ring, SHR_RDONLY | SHR_GET_APPDATA, 
                          &cast, &cast_len);
        if (CF.shr == NULL) goto done;
        rc = shr_stat(CF.shr, &stat, NULL);
        if (rc < 0) goto done;
        if (CF.mode == mode_status) {
          printf("%s, frames-ready %ld,"
               " frames-written %ld,"
               " frames-read %ld,"
               " frames-dropped %ld,"
               " byte-capacity %ld\n",
               CF.ring, stat.mu, stat.mw, stat.mr, stat.md, stat.bn);
        }
        if (CF.mode == mode_getcast) {
          printf("%.*s", (int)cast_len, cast);
        }
        shr_close(CF.shr);
        break;

      case mode_read:
        ccr = ccr_open(CF.ring, CCR_RDONLY, 0);
        if (ccr == NULL) goto done;
        if (ccr_getnext(ccr, CCR_BUFFER|CCR_JSON, &out, &len) < 0) {
          ccr_close(ccr);
          goto done;
        }
        printf("%.*s\n", (int)len, out);
        ccr_close(ccr);
        break;

      default: 
        assert(0);
        break;
    }
  }

  rc = 0;
 
 done:
  if (CF.file) free(CF.file);
  return 0;
}
