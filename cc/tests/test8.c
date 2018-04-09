#include <stdio.h>
#include "cc.h"

char *conf = __FILE__ "fg";   /* test1.c becomes test1.cfg */
#define adim(x) (sizeof(x)/sizeof(*x))

static void hexdump(char *buf, size_t len) {
  size_t i,n=0;
  unsigned char c;
  while(n < len) {
    fprintf(stdout,"%08x ", (int)n);
    for(i=0; i < 16; i++) {
      c = (n+i < len) ? buf[n+i] : 0;
      if (n+i < len) fprintf(stdout,"%.2x ", c);
      else fprintf(stdout, "   ");
    }
    for(i=0; i < 16; i++) {
      c = (n+i < len) ? buf[n+i] : ' ';
      if (c < 0x20 || c > 0x7e) c = '.';
      fprintf(stdout,"%c",c);
    }
    fprintf(stdout,"\n");
    n += 16;
  }
}

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

  s = ""; /* empty string */
  if (cc_capture(cc, &flat, &len) < 0) {
    printf("error\n");
    goto done;
  }

  hexdump(flat, len);

  i = cc_to_json(cc, &json, &json_len, flat, len, 0);
  if (i < 0) goto done;
  printf("%.*s\n", (int)json_len, json);

  printf("\n");

  i = cc_to_json(cc, &json, &json_len, flat, len, CC_PRETTY);
  if (i < 0) goto done;
  printf("%.*s\n", (int)json_len, json);

  cc_close(cc);

 done:
  return rc;
}
