#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "libut.h"
#include "ccr.h"

struct ccr {
  struct shr *shr;
  struct cc *cc;
  UT_string *tmp;
  int flags;
};

static int slurp(char *file, char **text, size_t *len) {
  int fd=-1, rc=-1;
  struct stat s;
  ssize_t nr;

  *text=NULL;
  *len = 0;

  if (stat(file, &s) == -1) {
    fprintf(stderr,"can't stat %s: %s\n", file, strerror(errno));
    goto done;
  }

  *len = s.st_size;
  if (*len == 0) { /* special case, empty file */
    rc=0;
    goto done;
  }

  fd = open(file, O_RDONLY);
  if (fd == -1) {
    fprintf(stderr,"can't open %s: %s\n", file, strerror(errno));
    goto done;
  }

  *text = malloc(*len);
  if (*text == NULL) {
    fprintf(stderr,"out of memory\n");
    goto done;
  }

  nr = read(fd, *text, *len);
  if (nr < 0) {
    fprintf(stderr,"read failed: %s\n", strerror(errno));
    goto done;
  }

  rc = 0;

 done:
  if ((rc < 0) && *text) { free(*text); *text=NULL; }
  if (fd != -1) close(fd);
  return rc;
}

static int validate_text(char *text, size_t len) {
  struct cc *cc;
  int rc = -1;

  /* invoke cc to parse the text to validate it */
  cc = cc_open(text, CC_BUFFER, len);
  if (cc == NULL) goto done;

  cc_close(cc);
  rc = 0;

 done:
  return rc;
}

int ccr_init(char *ring, size_t sz, int flags, ...) {
  int shr_flags = 0, rc = -1;
  char *file, *text = NULL;
  size_t len = 0;
  
  va_list ap;
  va_start(ap, flags);

  shr_flags |= SHR_MESSAGES;
  shr_flags |= SHR_APPDATA;
  if (flags & CCR_DROP)      shr_flags |= SHR_DROP;
  if (flags & CCR_KEEPEXIST) shr_flags |= SHR_KEEPEXIST;
  if (flags & CCR_OVERWRITE) shr_flags |= SHR_OVERWRITE;

  if (flags & CCR_CASTFILE) {
    file = va_arg(ap, char*);
    if (slurp(file, &text, &len) < 0) goto done;
  }

  /* eventually add other flags to set text */
  if ((flags & CCR_CASTFILE) == 0) goto done;

  if (validate_text(text, len) < 0) goto done;
  rc = shr_init(ring, sz, shr_flags, text, len);

 done:
  if (text) free(text);
  va_end(ap);
  return rc;
}

struct ccr *ccr_open(char *ring, int flags, ...) {
  int rc=-1, shr_mode=0;
  struct ccr *ccr=NULL;
  char *text;
  size_t len;

  /* must be least R or W, and not both */
  if (((flags & CCR_RDONLY) ^ (flags & CCR_WRONLY)) == 0) {
    fprintf(stderr,"ccr_open: invalid mode\n");
    goto done;
  }

  if (flags & CCR_RDONLY)   shr_mode |= SHR_RDONLY;
  if (flags & CCR_NONBLOCK) shr_mode |= SHR_NONBLOCK;
  if (flags & CCR_WRONLY)   shr_mode |= SHR_WRONLY;
  shr_mode |= SHR_GET_APPDATA;

  ccr = calloc(1, sizeof(*ccr));
  if (ccr == NULL) {
    fprintf(stderr,"ccr_open: out of memory\n");
    goto done;
  }

  /* open the ring, pull the cc text from appdata */
  ccr->shr = shr_open(ring, shr_mode, &text, &len);
  if (ccr->shr == NULL) goto done;
  ccr->cc = cc_open(text, CC_BUFFER, len);
  if (ccr->cc == NULL) goto done;
  utstring_new(ccr->tmp);
  ccr->flags = flags;
  rc = 0;

 done:
  if (rc < 0) {
    if (ccr && ccr->cc) cc_close(ccr->cc);
    if (ccr && ccr->shr) shr_close(ccr->shr);
    if (ccr && ccr->tmp) utstring_free(ccr->tmp);
    if (ccr) free(ccr);
    ccr = NULL;
  }
  return ccr;
}

int ccr_close(struct ccr *ccr) {
  cc_close(ccr->cc);
  shr_close(ccr->shr);
  utstring_free(ccr->tmp);
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

  wc = shr_write(ccr->shr, out, len);
  if (wc < 0) goto done;

  rc = 0;

 done:
  return rc;
}

/*
 * ccr_getnext
 *
 * Read one frame from the ring.
 * Block if ring empty, or return immediately in CCR_NONBLOCK mode.
 *
 * flags        varags                 description
 * -----       ---------------------   ---------------------
 * CCR_BUFFER  char**buf, size_t *len  get a volatile buffer
 * CCR_BUFFER|
 *  CCR_JSON   char**buf, size_t *len  get volatile json buffer
 *
 * returns:
 *   > 0 (number of bytes read from the ring)
 *   0   (ring empty, in non-blocking mode)
 *  -1   (error)
 *  -2   (signal arrived while blocked waiting for ring)
 *
 */
ssize_t ccr_getnext(struct ccr *ccr, int flags, ...) {
  ssize_t rc = -1;
  char *buf, **out;
  size_t len, *out_len;
  int fl = 0;

  va_list ap;
  va_start(ap, flags);

  /* 'buffer' is the only mode right now */
  if ((flags & CCR_BUFFER) == 0) goto done;
  if (flags & CCR_PRETTY) fl |= CC_PRETTY;

  /* get caller varargs */
  out = va_arg(ap, char**);
  out_len = va_arg(ap, size_t *);

 again: /* in case we need to grow recv buffer */

  /* use ccr->tmp internal buffer as a recv buf */
  buf = ccr->tmp->d;
  len = ccr->tmp->n;
  rc = shr_read(ccr->shr, buf, len);

  /* double if need more room in recv buffer */
  if (rc == -3) {
    utstring_reserve(ccr->tmp, (len ? (len * 2) : 100));
    goto again;
  }

  /* other error? */
  if (rc < 0) {
    fprintf(stderr, "ccr_getnext: error %ld\n", (long)rc);
    goto done;
  }

  /* no data? (nonblock mode) */
  if (rc == 0) goto done;

  /* want json encoding? */
  if (flags & CCR_JSON) {
    if (cc_to_json(ccr->cc, out, out_len, buf, len, fl)) rc = -1;
    /* on success, leave rc as is */
    goto done;
  }

  /* flat ccr buffer */
  *out = buf;
  *out_len = rc;

 done:
  va_end(ap);
  return rc;
}

int ccr_get_selectable_fd(struct ccr *ccr) {
  int rc = -1, fd;

  if ((ccr->flags & CCR_RDONLY) == 0) goto done;
  if ((ccr->flags & CCR_NONBLOCK) == 0) goto done;
  fd = shr_get_selectable_fd(ccr->shr);
  if (fd < 0) goto done;

  rc = 0;

 done:
  return (rc < 0) ? rc : fd;
}
