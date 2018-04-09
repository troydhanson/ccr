#include <stdio.h>
#include "cc.h"

char *conf = __FILE__ "fg";   /* test1.c becomes test1.cfg */
#define adim(x) (sizeof(x)/sizeof(*x))

int main() {
  int rc=-1, i;
  char *s;
  char *flat,*json;
  size_t len,json_len;

  struct cc_blob b;

  struct cc_map map[] = {
    {"name", CC_str, &s},
    {"bin", CC_blob, &b},
  };

  struct cc *cc;
  cc = cc_open(conf, CC_FILE);
  if (cc == NULL) goto done;

  rc = cc_mapv(cc, map, adim(map));
  if (rc < 0) goto done;

  s = "hello";
  b.buf = "\xbe\xef\x00\xff";
  b.len = 4;
  if (cc_capture(cc, &flat, &len) < 0) {
    printf("error\n");
    goto done;
  }

  i = cc_to_json(cc, &json, &json_len, flat, len, CC_PRETTY);
  if (i < 0) {
    printf("cc_to_json: error\n");
    goto done;
  }

  printf("%.*s\n", (int)json_len, json);

  cc_close(cc);

 done:
  return rc;
}
