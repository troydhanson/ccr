#include <string.h>
#include <assert.h>
#include <stddef.h>
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
  int rc=-1, sc, i, dislen;
  char *flat, *json;
  size_t len, jlen;
  setlinebuf(stdout);

  char *s = "unused";
  char *name = "Abraham";
  char *type = "A";

#define MAPLEN 3
  struct cc_map map[MAPLEN] = {
    { "not_in_cast",  CC_str, &s }, 
    { "name",         CC_str, &name }, 
    { "type",         CC_str, &type }, 
  };

  struct cc *cc;
  cc = cc_open(conf, CC_FILE);
  if (cc == NULL) goto done;

  sc = cc_mapv(cc, map, adim(map));
  if (sc < 0) {
    fprintf(stderr, "cc_mapv: failed\n");
    goto done;
  }

  /* populate struct and capture */
  sc = cc_capture(cc, &flat, &len);
  if (sc < 0) {
    fprintf(stderr, "cc_capture: failed\n");
    goto done;
  }

  /* examine flat buffer and its json */
  hexdump(flat, len);
  sc = cc_to_json(cc, &json, &jlen, flat, len, 0);
  if (sc < 0) {
    fprintf(stderr, "cc_to_json: failed\n");
    goto done;
  }
  printf("\n%.*s\n", (int)jlen, json);

  /* dissect fields */
  struct cc_map *dismap=NULL;
  sc = cc_dissect(cc, &dismap, &dislen, flat, len, 0);
  if (sc < 0) {
    fprintf(stderr, "cc_dissect: failed\n");
    goto done;
  }

  for(i = 0; i < dislen; i++) {
    printf("%d name %s off %td type %d\n", i,
      dismap[i].name,
      (ptrdiff_t)((char*)dismap[i].addr - flat),
      dismap[i].type);
  }

  cc_close(cc);

 done:
  return rc;
}
