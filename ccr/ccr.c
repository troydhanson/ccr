#include <stdlib.h>
#include <assert.h>
#include "ccr.h"

struct ccr {
  struct shr *shr;
  struct cc *cc;
  int flags;
};

int ccr_init(char *ring, size_t sz, int flags) {
  int shr_flags = SHR_MESSAGES;
  if (flags & CCR_DROP)      shr_flags |= SHR_DROP;
  if (flags & CCR_KEEPEXIST) shr_flags |= SHR_KEEPEXIST;
  if (flags & CCR_OVERWRITE) shr_flags |= SHR_OVERWRITE;
  return shr_init(ring, sz, shr_flags);
}

struct ccr *ccr_open(char *ccfile, char *ring, int flags, ...) {
  int rc=-1, shr_mode=0;
  struct ccr *ccr=NULL;

  if (((flags & (CCR_RDONLY | CCR_WRONLY)) == 0) ||
      ((flags & CCR_RDONLY) && (flags & CCR_WRONLY))) {
    fprintf(stderr,"ccr_open: invalid mode\n");
    goto done;
  }

  if (flags & CCR_RDONLY) shr_mode = SHR_RDONLY;
  if (flags & CCR_WRONLY) shr_mode = SHR_WRONLY;

  ccr = calloc(1, sizeof(*ccr));
  if (ccr == NULL) goto done;

  ccr->flags = flags;
  ccr->cc = cc_open(ccfile, 0);
  if (ccr->cc == NULL) goto done;

  ccr->shr = shr_open(ring, shr_mode);
  if (ccr->shr == NULL) goto done;

  rc = 0;

 done:
  if (rc < 0) {
    if (ccr && ccr->cc) cc_close(ccr->cc);
    if (ccr && ccr->shr) shr_close(ccr->shr);
    if (ccr) free(ccr);
    ccr = NULL;
  }
  return ccr;
}

int ccr_close(struct ccr *ccr) {
  cc_close(ccr->cc);
  shr_close(ccr->shr);
  free(ccr);
  return 0;
}

int ccr_mapv(struct ccr *ccr, struct cc_map *map, int count) {
  return cc_mapv(ccr->cc, map, count);
}

int ccr_capture(struct ccr *ccr) {
  int rc=-1, sc;
  size_t len;
  ssize_t wc;
  char *out;

  assert(ccr->flags & CCR_WRONLY);

  sc = cc_capture(ccr->cc, &out, &len);
  if (sc < 0) goto done;

  // if (ccr->flags & CCR_BUFFER) /* TODO use buffer / shr_writev */

  wc = shr_write(ccr->shr, out, len);
  if (wc < 0) goto done;

  rc = 0;

 done:
  return rc;
}

