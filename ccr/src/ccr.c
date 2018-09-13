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
  struct cc_map *dissect_map;
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

/*
 * get_format
 *
 * copies (using malloc) the format text from given ccr ring
 * caller must free eventually
 *
 * returns
 *   0 success (and fills in text and len)
 *  -1 error
 *
 * returns
 */
static int get_format(char *file, char **text, size_t *len) {
  struct shr *s = NULL;
  int rc=-1, sc;

  *text=NULL;
  *len = 0;

  s = shr_open(file, SHR_RDONLY);
  if (s == NULL) goto done;

  sc = shr_appdata(s, (void**)text, NULL, len);
  if (sc < 0) {
    fprintf(stderr,"ccr_open: text not found\n");
    goto done;
  }

  assert(*text && *len);
  rc = 0;

 done:
  if (s) shr_close(s);
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
  int shr_flags, rc = -1, sc, need_free=0, nmodes=0;
  char *file, *text = NULL;
  size_t len = 0;
  
  va_list ap;
  va_start(ap, flags);

  shr_flags = SHR_APPDATA;
  if (flags & CCR_KEEPEXIST) shr_flags |= SHR_KEEPEXIST;
  if (flags & CCR_DROP)      shr_flags |= SHR_DROP;
  if (flags & CCR_FARM)      shr_flags |= SHR_FARM;
  if (flags & CCR_MLOCK)     shr_flags |= SHR_MLOCK;
  if (flags & CCR_SYNC)      shr_flags |= SHR_SYNC;

  nmodes += (flags & CCR_CASTFILE) ? 1 : 0;
  nmodes += (flags & CCR_CASTCOPY) ? 1 : 0;
  nmodes += (flags & CCR_CASTTEXT) ? 1 : 0;
  if (nmodes != 1) {
    fprintf(stderr, "ccr_init: flags allow only one of: "
                    "CCR_CASTFILE CCR_CASTCOPY CCR_CASTTEXT\n");
    goto done;
  }

  if (flags & CCR_CASTFILE) {
    file = va_arg(ap, char*);
    sc = slurp(file, &text, &len);
    if (sc < 0) goto done;
    need_free=1;
  }

  if (flags & CCR_CASTCOPY) {
    file = va_arg(ap, char*);
    sc = get_format(file, &text, &len);
    if (sc < 0) goto done;
    need_free=1;
  }

  if (flags & CCR_CASTTEXT) {
    text = va_arg(ap, char*);
    len = va_arg(ap, size_t);
  }

  assert(text && len);
  if (validate_text(text, len) < 0) goto done;
  rc = shr_init(ring, sz, shr_flags, text, len);

 done:
  if (text && need_free) free(text);
  va_end(ap);
  return rc;
}

struct ccr *ccr_open(char *ring, int flags, ...) {
  int sc, rc=-1, shr_mode=0;
  struct ccr *ccr=NULL;
  char *text=NULL;
  size_t len;

  /* must be least R or W, and not both */
  if (((flags & CCR_RDONLY) ^ (flags & CCR_WRONLY)) == 0) {
    fprintf(stderr,"ccr_open: invalid mode\n");
    goto done;
  }

  if (flags & CCR_RDONLY)   shr_mode |= SHR_RDONLY;
  if (flags & CCR_NONBLOCK) shr_mode |= SHR_NONBLOCK;
  if (flags & CCR_WRONLY)   shr_mode |= SHR_WRONLY;
  if (flags & CCR_BUFFER)   shr_mode |= SHR_BUFFERED;

  ccr = calloc(1, sizeof(*ccr));
  if (ccr == NULL) {
    fprintf(stderr,"ccr_open: out of memory\n");
    goto done;
  }

  ccr->shr = shr_open(ring, shr_mode);
  if (ccr->shr == NULL) goto done;

  sc = shr_appdata(ccr->shr, (void**)&text, NULL, &len);
  if (sc < 0) {
    fprintf(stderr,"ccr_open: text not found\n");
    goto done;
  }

  assert(text && len);
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
  if (text) free(text);
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
 * flags                   varags                 description
 * -----                  ---------------------   ---------------------
 * CCR_BUFFER             char**buf, size_t *len  get volatile buffer
 * CCR_BUFFER| CCR_JSON   char**buf, size_t *len  get volatile json buffer
 * CCR_RESTORE            n/a                     unpack to caller memory
 *
 * if CCR_JSON is specified, CCR_PRETTY and CCR_NEWLINE may be
 * OR'd to pretty-print the JSON and append a newline respectively.
 *
 * CCR_LEN4FIRST can be OR'd to get the buffer prepended with a 
 * 4-byte native endian length prefix. (not supported in CCR_JSON)
 *
 * CCR_RESTORE reads a frame and unpacks the fields back to previously
 * ccr_mapv'd caller memory. (not supported in CCR_JSON)
 *
 * returns:
 *   > 0   (success; data was read from ring)
 *     0   (ring empty, in non-blocking mode)
 *    -1   (error)
 *
 */
ssize_t ccr_getnext(struct ccr *ccr, int flags, ...) {
  ssize_t nr = -1;
  char *buf, **out;
  size_t avail, *out_len;
  int fl = 0, sc, v=0;

  va_list ap;
  va_start(ap, flags);

  /* must be one mode or the other */
  v += (flags & CCR_BUFFER) ?  1 : 0;
  v += (flags & CCR_RESTORE) ? 1 : 0;
  if (v != 1) goto done;

  if (flags & CCR_PRETTY)  fl |= CC_PRETTY;
  if (flags & CCR_NEWLINE) fl |= CC_NEWLINE;


 again: /* in case we need to grow recv buffer */

  /* use ccr->tmp internal buffer as a recv buf */
  assert(ccr->tmp->n > sizeof(uint32_t));
  buf = ccr->tmp->d + sizeof(uint32_t);
  avail = ccr->tmp->n - sizeof(uint32_t);
  nr = shr_read(ccr->shr, buf, avail);

  /* double if need more room in recv buffer */
  if (nr == -2) {
    utstring_reserve(ccr->tmp, (avail ? (avail * 2) : 100));
    goto again;
  }

  /* other error? */
  if (nr < 0) {
    fprintf(stderr, "ccr_getnext: error %zd\n", nr);
    goto done;
  }

  /* no data? (nonblock mode) */
  if (nr == 0) goto done;

  /* BUFFER is the first major mode */
  if (flags & CCR_BUFFER) {

    assert((flags & CCR_RESTORE) == 0);

    /* get caller varargs */
    out = va_arg(ap, char**);
    out_len = va_arg(ap, size_t *);

    if (flags & CCR_JSON) {
      sc = cc_to_json(ccr->cc, out, out_len, buf, nr, fl);
      if (sc < 0) nr = -1;
    } else {
      if (flags & CCR_LEN4FIRST) {
        if (nr > UINT32_MAX) {
          fprintf(stderr, "frame exceeds 32-bit length\n");
          goto done;
        }
        uint32_t len32 = (uint32_t)nr;
        assert(buf == (ccr->tmp->d + sizeof(uint32_t)));
        buf = ccr->tmp->d;
        memcpy(buf, &len32, sizeof(len32));
        nr += sizeof(len32);
      }

      *out = buf;
      *out_len = nr;
   }
  }

  /* RESTORE is the second major mode */
  if (flags & CCR_RESTORE) {
    assert((flags & CCR_BUFFER) == 0);
    sc = cc_restore(ccr->cc, buf, nr, 0);
    if (sc < 0) nr = -1;
  }

 done:
  va_end(ap);
  return nr;
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

/*
 * ccr_flush
 *
 * flush buffered data (in CCR_WRONLY|CCR_BUFFER mode)
 *
 * NOTE
 *  for a ccr ring in CCR_NONBLOCK mode, this may return 0
 *  if the ring is too full to accept the buffered data
 *
 * wait parameter:
 *  if non-zero, causes a blocking flush. this only matters
 *  if the ccr open mode was CCR_NONBLOCK.
 *
 * returns
 * >= 0 bytes written
 *  < 0 error
 */ 
ssize_t ccr_flush(struct ccr *ccr, int wait) {
  ssize_t nr;

  nr = shr_flush(ccr->shr, wait);
  return nr;
}

/*
 * ccr_dissect
 *
 * given a flat cc image (from a ccr_getnext)
 * dissect the buffer into its fields. this 
 * function populates a cc_map[] and stores its
 * pointer into the map output parameter, of
 * *count elements, occupying a volatile 
 * buffer in the ccr structure. it will be 
 * overwritten on the next call to ccr_dissect.
 *
 * the map contains elements of the form,
 *
 *   map[n].name  - C string with field name
 *   map[n].addr  - pointer into input buffer
 *   map[n].type  - CC_i8, CC_i16, etc field type
 *
 * returns
 *   0 success
 *  -1 error
 *
 */
int ccr_dissect(struct ccr *ccr, struct cc_map **map, int *count,
       char *in, size_t in_len, int flags) {
  int sc;

  sc = cc_dissect(ccr->cc, map, count, in, in_len, flags);
  return sc;
}

