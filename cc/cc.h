#ifndef __CC_H__
#define __CC_H__

/* flags */
#define CC_TO_JSON      (1U << 0)
#define CC_FROM_JSON    (1U << 1)
#define CC_PRETTY       (1U << 2)

/* TODO u32 etc */
#define TYPES x(i8) x(i16) x(i32) x(str) x(d64) x(ipv4) x(mac)
#define x(t) CC_ ## t,
typedef enum { TYPES } cc_type;
#undef x

/* defined internally in cc.c; named here for type safety */
struct cc;

/* mapping structure */
struct cc_map {
  char *name;
  cc_type type;
  void *addr;
};

/* API */
struct cc * cc_open(char *file, int flags, ...);
int cc_close(struct cc *cc);

/* associate fields with caller memory locations */
int cc_mapv(struct cc *cc, struct cc_map *map, int count);

/* pack caller memory to flattened buffer */
int cc_capture(struct cc *cc, char **out, size_t *len);
int cc_convert(struct cc *cc, char **out, size_t *out_len,
       char *in, size_t in_len, int flags);

/* reads flattened buffer, unpack to caller memory */
int cc_restore(struct cc *cc, char *flat, size_t len);

#endif // __CC_H__
