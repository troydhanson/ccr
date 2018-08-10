#ifndef __CC_INTERNAL_H__
#define __CC_INTERNAL_H__

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>
#include <stdarg.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include "libut.h"
#include "cc.h"

#include <jansson.h>

#define adim(a) (sizeof(a)/sizeof(*a))

struct cc {
  UT_vector /* of UT_string */ names;
  UT_vector /* of int       */ output_types; /* enum (CC_i16 CC_i32) etc */
  UT_vector /* of UT_string */ defaults;     /* pack w/o map uses this default */
  UT_vector /* of void* */     caller_addrs; /* caller pointer to copy data from */
  UT_vector /* of int       */ caller_types; /* caller pointer type i16 i32 etc */
  UT_vector /* struct cc_map */dissect_map;  /* fulfills cc_dissect */
  UT_string flat;                            /* concatenated packed values buffer */
  UT_string tmp;
  json_t *json;
};

const UT_mm ptr_mm;
const UT_mm cc_mm;

/* we have a table of conversion functions, which have this signature */
typedef int (*xcpf)(UT_string *to, void *from); /* caller memory -> cc */
xcpf cc_conversions[CC_MAX][CC_MAX];
int slot_to_json(cc_type ot, char *from, size_t from_len, json_t **j);

#endif // _CC_INTERNAL_H__
