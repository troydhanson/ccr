#include "cc-internal.h"

/*
 * blob_encode
 *
 * simple encoder of binary to safe string. caller must free!
 */
static char hex[16] = {'0','1','2','3','4','5','6','7','8','9',
                       'a','b','c','d','e','f'};
static char *blob_encode(char *from, size_t len, size_t *enc_len) {
  char *enc, *e;
  unsigned char f;

  enc = malloc(len * 2);
  if (enc == NULL) return NULL;

  *enc_len = len * 2;
  e = enc;
  while(len) {
    f = (unsigned char)*from;
    *e = hex[(f & 0xf0) >> 4];
    e++;
    *e = hex[(f & 0x0f)];
    e++;
    from++;
    len--;
  }
  return enc;
}

/*
 * slot_to_json
 *
 * convert a single item from flat cc buffer 
 * to a refcounted json value. 
 *
 * the caller is expected to steal the reference 
 * (e.g. json_set_object_new).
 *
 * return 
 *    bytes consumed (into from) on success
 *    or -1 on error 
 *
 */
int slot_to_json(cc_type ot, char *from, size_t len, json_t **j) {
  int rc = -1;
  unsigned l=0;
  int8_t i8;
  uint8_t u8;
  int16_t i16;
  uint16_t u16;
  int32_t i32;
  uint32_t u32;
  char s[20];
  char ip4str[INET_ADDRSTRLEN];
  char ip6str[INET6_ADDRSTRLEN];
  struct in6_addr ia6;
  unsigned char *m;
  double f;
  char *enc;
  size_t enc_len;

  switch(ot) {
    case CC_i8:
      l = sizeof(int8_t);
      if (len < l) goto done;
      memcpy(&i8, from, l);
      *j = json_integer(i8);
      if (*j == NULL) goto done;
      break;
    case CC_u16:
      l = sizeof(uint16_t);
      if (len < l) goto done;
      memcpy(&u16, from, l);
      *j = json_integer(u16);
      if (*j == NULL) goto done;
      break;
    case CC_i16:
      l = sizeof(int16_t);
      if (len < l) goto done;
      memcpy(&i16, from, l);
      *j = json_integer(i16);
      if (*j == NULL) goto done;
      break;
    case CC_i32:
      l = sizeof(int32_t);
      if (len < l) goto done;
      memcpy(&i32, from, l);
      *j = json_integer(i32);
      if (*j == NULL) goto done;
      break;
    case CC_blob:
      l = sizeof(uint32_t);
      if (len < l) goto done;
      memcpy(&u32, from, l);
      from += l;
      len -= l;
      if ((u32 > 0) && (len < u32)) goto done;
      l += u32;
      enc = blob_encode(from, u32, &enc_len);
      if (enc == NULL) goto done;
      *j = json_stringn(enc, enc_len);
      free(enc);
      if (*j == NULL) goto done;
      break;
    case CC_str8:
      l = sizeof(uint8_t);
      if (len < l) goto done;
      memcpy(&u8, from, l);
      from += l;
      len -= l;
      if ((u8 > 0) && (len < u8)) goto done;
      l += u8;
      *j = json_stringn(from, u8);
      if (*j == NULL) goto done;
      break;
    case CC_str:
      l = sizeof(uint32_t);
      if (len < l) goto done;
      memcpy(&u32, from, l);
      from += l;
      len -= l;
      if ((u32 > 0) && (len < u32)) goto done;
      l += u32;
      *j = json_stringn(from, u32);
      if (*j == NULL) goto done;
      break;
    case CC_d64:
      l = sizeof(double);
      if (len < l) goto done;
      memcpy(&f, from, l);
      *j = json_real(f);
      if (*j == NULL) goto done;
      break;
    case CC_ipv46:
      l = sizeof(uint8_t);
      if (len < l) goto done;
      memcpy(&u8, from, l);
      assert((u8 == 4) || (u8 == 16));
      l += u8;
      from += sizeof(uint8_t);
      len -=  sizeof(uint8_t);
      if (len < u8) goto done;
      if (u8 == 16) {
        memcpy(&ia6, from, sizeof(struct in6_addr));
        if (inet_ntop(AF_INET6, &ia6, ip6str, sizeof(ip6str)) == NULL)
          goto done;
        *j = json_string(ip6str);
        if (*j == NULL) goto done;
      } else {
        assert(u8 == 4);
        memcpy(&u32, from, sizeof(u32));
        if (inet_ntop(AF_INET, &u32, ip4str, sizeof(ip4str)) == NULL) goto done;
        *j = json_string(ip4str);
        if (*j == NULL) goto done;
      }
      break;
    case CC_ipv4:
      l = sizeof(uint32_t);
      if (len < l) goto done;
      memcpy(&u32, from, l);
      if (inet_ntop(AF_INET, &u32, ip4str, sizeof(ip4str)) == NULL) goto done;
      *j = json_string(ip4str);
      if (*j == NULL) goto done;
      break;
    case CC_mac:
      l = sizeof(char) * 6;
      if (len < l) goto done;
      m = (unsigned char*)from;
      snprintf(s, sizeof(s), "%x:%x:%x:%x:%x:%x", (int)m[0], (int)m[1], (int)m[2],  
                                          (int)m[3], (int)m[4], (int)m[5]);
      *j = json_string(s);
      if (*j == NULL) goto done;
      break;
    default:
      assert(0);
      goto done;
      break;
  }

  rc = 0;

 done:
  return (rc < 0) ? rc : (int)l;
}

