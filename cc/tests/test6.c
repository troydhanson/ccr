#include <stdio.h>
#include "cc.h"

char *conf = __FILE__ "fg";   /* test1.c becomes test1.cfg */
#define adim(x) (sizeof(x)/sizeof(*x))

int main() {
  int rc=-1, i;
  char *s;
  char *flat,*json;
  size_t len,json_len;

  struct cc_map map[] = {
    {"name", CC_str, &s},
  };

  struct cc *cc;
  cc = cc_open(conf, CC_FILE);
  if (cc == NULL) goto done;

  rc = cc_mapv(cc, map, adim(map));
  if (rc < 0) goto done;

  s = "hello";
  if (cc_capture(cc, &flat, &len) < 0) {
    printf("error\n");
    goto done;
  }

  printf("\n");

  i = cc_to_json(cc, &json, &json_len, flat, len, CC_PRETTY);
  if (i < 0) goto done;
  printf("%.*s\n", (int)json_len, json);

  /**********************************************************
   * now repeat a map with a different variable. we should
   * see that the previous mapping was cleared automatically
   *********************************************************/
  struct cc_map map2[] = {
    {"handle", CC_str, &s},
  };

  rc = cc_mapv(cc, map2, adim(map2));
  if (rc < 0) goto done;

  s = "handlebar";
  if (cc_capture(cc, &flat, &len) < 0) {
    printf("error\n");
    goto done;
  }

  printf("\n");

  i = cc_to_json(cc, &json, &json_len, flat, len, CC_PRETTY);
  if (i < 0) goto done;
  printf("%.*s\n", (int)json_len, json);

  cc_close(cc);

 done:
  return rc;
}
