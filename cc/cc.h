#ifndef __CC_H__
#define __CC_H__

/* flags */
#define CC_PRETTY       (1U << 1)
#define CC_FILE         (1U << 2)
#define CC_BUFFER       (1U << 3)

#define TYPES x(i8) x(i16) x(u16) x(i32) x(str) x(d64) x(ipv4) x(mac)
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
struct cc * cc_open(char *file_or_text, int flags, ...);
int cc_close(struct cc *cc);

/* associate fields with caller memory locations */
int cc_mapv(struct cc *cc, struct cc_map *map, int count);

/* pack caller memory to flattened buffer */
int cc_capture(struct cc *cc, char **out, size_t *len);
int cc_to_json(struct cc *cc, char **out, size_t *out_len,
       char *in, size_t in_len, int flags);

/* reads flattened buffer, unpack to caller memory */
int cc_restore(struct cc *cc, char *flat, size_t len);

#endif // __CC_H__
