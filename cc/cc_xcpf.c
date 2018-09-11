#include "cc-internal.h"

static int xcpf_u16_u16(UT_string *d, void *p, int flags) {
  utstring_bincpy(d, p, sizeof(uint16_t));
  return 0;
}

static int xcpf_i16_i16(UT_string *d, void *p, int flags) {
  utstring_bincpy(d, p, sizeof(int16_t));
  return 0;
}

static int xcpf_i32_i32(UT_string *d, void *p, int flags) {
  utstring_bincpy(d, p, sizeof(int32_t));
  return 0;
}

static int xcpf_ipv4_ipv4(UT_string *d, void *p, int flags) {
  utstring_bincpy(d, p, sizeof(int32_t));
  return 0;
}

static int xcpf_ipv46_ipv46(UT_string *d, void *p, int flags) {
  uint8_t u8;
  memcpy(&u8, p, sizeof(u8));
  assert((u8 == 4) || (u8 == 16));
  utstring_bincpy(d, p, sizeof(uint8_t) + u8);
  return 0;
}

/*****************************************************************/
static int xcpf_str_u16(UT_string *d, void *p, int flags) {
  if (flags & CC_FLAT2MEM) return -1;
  char **c = (char **)p;
  unsigned u;
  if (sscanf(*c, "%u", &u) != 1) return -1;
  uint16_t u16 = u; // may truncate
  utstring_bincpy(d, &u16, sizeof(u16));
  return 0;
}

static int xcpf_str_i16(UT_string *d, void *p, int flags) {
  if (flags & CC_FLAT2MEM) return -1;
  char **c = (char **)p;
  int i;
  if (sscanf(*c, "%d", &i) != 1) return -1;
  int16_t i16 = i; // may truncate
  utstring_bincpy(d, &i16, sizeof(i16));
  return 0;
}

static int xcpf_str_i32(UT_string *d, void *p, int flags) {
  if (flags & CC_FLAT2MEM) return -1;
  char **c = (char **)p;
  int i;
  if (sscanf(*c, "%d", &i) != 1) return -1;
  int32_t i32 = i;
  utstring_bincpy(d, &i32, sizeof(i32));
  return 0;
}

static int xcpf_str_ipv4(UT_string *d, void *p, int flags) {
  if (flags & CC_FLAT2MEM) return -1;
  char **s = (char **)p;
  struct in_addr ia;
  assert(sizeof(ia) == 4);
  if (inet_pton(AF_INET, *s, &ia) <= 0)
    return -1;
  utstring_bincpy(d, &ia, sizeof(ia));
  return 0;
}

static int xcpf_str_ipv46(UT_string *d, void *p, int flags) {
  if (flags & CC_FLAT2MEM) return -1;
  char **s = (char **)p;
  struct in_addr ia4;
  struct in6_addr ia6;
  uint8_t u8;
  int sc;

  sc = inet_pton(AF_INET, *s, &ia4);
  if (sc == 1) {
    u8 = 4;
    assert(sizeof(ia4) == 4);
    utstring_bincpy(d, &u8, sizeof(uint8_t));
    utstring_bincpy(d, &ia4, sizeof(ia4));
    return 0;
  }

  sc = inet_pton(AF_INET6, *s, &ia6);
  if (sc == 1) {
    u8 = 16;
    assert(sizeof(ia6) == 16);
    utstring_bincpy(d, &u8, sizeof(uint8_t));
    utstring_bincpy(d, &ia6, sizeof(ia6));
    return 0;
  }

  return -1;
}

static int xcpf_str_str8(UT_string *d, void *p, int flags) {
  if (flags & CC_FLAT2MEM) return -1;
  char **c = (char **)p;
  uint32_t l = strlen(*c);
  if (l > 255) return -1;
  uint8_t u8 = (uint8_t)l;
  utstring_bincpy(d, &u8, sizeof(u8));
  if (l) utstring_printf(d, "%s", *c);
  return 0;
}

static int xcpf_str_str(UT_string *d, void *p, int flags) {

  if (flags & CC_FLAT2MEM) {
    char *c;
    uint32_t l;
    memcpy(&l, p, sizeof(l));
    c = (char*)p + sizeof(l);
    utstring_bincpy(d, c, l);
    utstring_bincpy(d, "\0", 1);
    return 0;
  }

  assert(flags & CC_MEM2FLAT);
  char **c = (char **)p;
  uint32_t l = strlen(*c);
  utstring_bincpy(d, &l, sizeof(l));
  if (l) utstring_printf(d, "%s", *c);
  return 0;
}

static int xcpf_str_blob(UT_string *d, void *p, int flags) {
  if (flags & CC_FLAT2MEM) return -1;
  char **c = (char **)p;
  uint32_t l = strlen(*c);
  utstring_bincpy(d, &l, sizeof(l));
  if (l) utstring_printf(d, "%s", *c);
  return 0;
}

static int xcpf_str_i8(UT_string *d, void *p, int flags) {
  if (flags & CC_FLAT2MEM) return -1;
  char **c = (char **)p;
  int i;
  if (sscanf(*c, "%d", &i) != 1) return -1;
  int8_t i8 = i; // may truncate
  utstring_bincpy(d, &i8, sizeof(i8));
  return 0;
}

static int xcpf_str_d64(UT_string *d, void *p, int flags) {
  if (flags & CC_FLAT2MEM) return -1;
  char **c = (char **)p;
  double f;
  if (sscanf(*c, "%lf", &f) != 1) return -1;
  utstring_bincpy(d, &f, sizeof(f));
  return 0;
}

static int xcpf_str_mac(UT_string *d, void *p, int flags) {
  if (flags & CC_FLAT2MEM) return -1;
  char **c = (char **)p;
  unsigned int ma, mb, mc, md, me, mf;
  if (sscanf(*c, "%x:%x:%x:%x:%x:%x", &ma,&mb,&mc,&md,&me,&mf) != 6) return -1;
  if ((ma > 255) || (mb > 255) || (mc > 255) || 
      (md > 255) || (me > 255) || (mf > 255)) return -1;
  utstring_printf(d, "%c%c%c%c%c%c", ma, mb, mc, md, me, mf);
  return 0;
}

/*****************************************************************/

static int xcpf_i8_i8(UT_string *d, void *p, int flags) {
  utstring_bincpy(d, p, sizeof(int8_t));
  return 0;
}

static int xcpf_d64_d64(UT_string *d, void *p, int flags) {
  utstring_bincpy(d, p, sizeof(double));
  return 0;
}

static int xcpf_mac_mac(UT_string *d, void *p, int flags) {
  utstring_bincpy(d, p, 6*sizeof(char));
  return 0;
}

static int xcpf_blob_blob(UT_string *d, void *p, int flags) {
  if (flags & CC_FLAT2MEM) {
    /* put length at front just as it was in flat,
     * then cc_restore pokes into caller struct */
    uint32_t l;
    memcpy(&l, p, sizeof(l));
    utstring_bincpy(d, p, sizeof(l) + l);
    return 0;
  }

  assert(flags & CC_MEM2FLAT);
  struct cc_blob *bp = (struct cc_blob*)p;
  uint32_t l = (uint32_t)bp->len;
  utstring_bincpy(d, &l, sizeof(l));
  if (l) utstring_bincpy(d, bp->buf, l);
  return 0;
}

xcpf cc_conversions[/*from*/CC_MAX][/*to*/CC_MAX] = {
  [CC_i16][CC_u16] = NULL,
  [CC_i16][CC_i16] = xcpf_i16_i16,
  [CC_i16][CC_i32] = NULL,
  [CC_i16][CC_ipv4] = NULL,
  [CC_i16][CC_ipv46] = NULL,
  [CC_i16][CC_str] = NULL,
  [CC_i16][CC_str8] = NULL,
  [CC_i16][CC_i8] = NULL,
  [CC_i16][CC_d64] = NULL,
  [CC_i16][CC_mac] = NULL,
  [CC_i16][CC_blob] = NULL,

  [CC_u16][CC_u16] = xcpf_u16_u16,
  [CC_u16][CC_i16] = NULL,
  [CC_u16][CC_i32] = NULL,
  [CC_u16][CC_ipv4] = NULL,
  [CC_u16][CC_ipv46] = NULL,
  [CC_u16][CC_str] = NULL,
  [CC_u16][CC_str8] = NULL,
  [CC_u16][CC_i8] = NULL,
  [CC_u16][CC_d64] = NULL,
  [CC_u16][CC_mac] = NULL,
  [CC_u16][CC_blob] = NULL,

  [CC_i32][CC_u16] = NULL,
  [CC_i32][CC_i16] = NULL,
  [CC_i32][CC_i32] = xcpf_i32_i32,
  [CC_i32][CC_ipv4] = NULL,
  [CC_i32][CC_ipv46] = NULL,
  [CC_i32][CC_str] = NULL,
  [CC_i32][CC_str8] = NULL,
  [CC_i32][CC_i8] = NULL,
  [CC_i32][CC_d64] = NULL,
  [CC_i32][CC_mac] = NULL,
  [CC_i32][CC_blob] = NULL,

  [CC_ipv4][CC_u16] = NULL,
  [CC_ipv4][CC_i16] = NULL,
  [CC_ipv4][CC_i32] = NULL,
  [CC_ipv4][CC_ipv4] = xcpf_ipv4_ipv4,
  [CC_ipv4][CC_ipv46] = NULL,
  [CC_ipv4][CC_str] = NULL,
  [CC_ipv4][CC_str8] = NULL,
  [CC_ipv4][CC_i8] = NULL,
  [CC_ipv4][CC_d64] = NULL,
  [CC_ipv4][CC_mac] = NULL,
  [CC_ipv4][CC_blob] = NULL,

  [CC_ipv46][CC_u16] = NULL,
  [CC_ipv46][CC_i16] = NULL,
  [CC_ipv46][CC_i32] = NULL,
  [CC_ipv46][CC_ipv4] = NULL,
  [CC_ipv46][CC_ipv46] = xcpf_ipv46_ipv46,
  [CC_ipv46][CC_str] = NULL,
  [CC_ipv46][CC_str8] = NULL,
  [CC_ipv46][CC_i8] = NULL,
  [CC_ipv46][CC_d64] = NULL,
  [CC_ipv46][CC_mac] = NULL,
  [CC_ipv46][CC_blob] = NULL,

  [CC_str][CC_u16] = xcpf_str_u16,
  [CC_str][CC_i16] = xcpf_str_i16,
  [CC_str][CC_i32] = xcpf_str_i32,
  [CC_str][CC_ipv4] = xcpf_str_ipv4,
  [CC_str][CC_ipv46] = xcpf_str_ipv46,
  [CC_str][CC_str] = xcpf_str_str,
  [CC_str][CC_str8] = xcpf_str_str8,
  [CC_str][CC_i8] = xcpf_str_i8,
  [CC_str][CC_d64] = xcpf_str_d64,
  [CC_str][CC_mac] = xcpf_str_mac,
  [CC_str][CC_blob] = xcpf_str_blob,

  [CC_i8][CC_u16] = NULL,
  [CC_i8][CC_i16] = NULL,
  [CC_i8][CC_i32] = NULL,
  [CC_i8][CC_ipv4] = NULL,
  [CC_i8][CC_ipv46] = NULL,
  [CC_i8][CC_str] = NULL,
  [CC_i8][CC_str8] = NULL,
  [CC_i8][CC_i8] = xcpf_i8_i8,
  [CC_i8][CC_d64] = NULL,
  [CC_i8][CC_mac] = NULL,
  [CC_i8][CC_blob] = NULL,

  [CC_d64][CC_u16] = NULL,
  [CC_d64][CC_i16] = NULL,
  [CC_d64][CC_i32] = NULL,
  [CC_d64][CC_ipv4] = NULL,
  [CC_d64][CC_ipv46] = NULL,
  [CC_d64][CC_str] = NULL,
  [CC_d64][CC_str8] = NULL,
  [CC_d64][CC_i8] = NULL,
  [CC_d64][CC_d64] = xcpf_d64_d64,
  [CC_d64][CC_mac] = NULL,
  [CC_d64][CC_blob] = NULL,

  [CC_mac][CC_u16] = NULL,
  [CC_mac][CC_i16] = NULL,
  [CC_mac][CC_i32] = NULL,
  [CC_mac][CC_ipv4] = NULL,
  [CC_mac][CC_str] = NULL,
  [CC_mac][CC_str8] = NULL,
  [CC_mac][CC_i8] = NULL,
  [CC_mac][CC_d64] = NULL,
  [CC_mac][CC_mac] = xcpf_mac_mac,
  [CC_mac][CC_blob] = NULL,

  [CC_blob][CC_u16] = NULL,
  [CC_blob][CC_i16] = NULL,
  [CC_blob][CC_i32] = NULL,
  [CC_blob][CC_ipv4] = NULL,
  [CC_blob][CC_ipv46] = NULL,
  [CC_blob][CC_str] = NULL,
  [CC_blob][CC_str8] = NULL,
  [CC_blob][CC_i8] = NULL,
  [CC_blob][CC_d64] = NULL,
  [CC_blob][CC_mac] = NULL,
  [CC_blob][CC_blob] = xcpf_blob_blob,
};

