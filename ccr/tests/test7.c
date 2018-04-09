#include <stdio.h>
#include <string.h>
#include "ccr.h"

char *ccfile = __FILE__ "fg";   /* test1.c becomes test1.cfg */
char *ring = __FILE__ ".ring";  /* test1.c becomes test1.c.ring */
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
  int rc=-1, dislen;
  struct cc_map *dis=NULL;
  int32_t i, i32;
  char *s,*h;
  struct cc_map map[] = {
    {"name", CC_str, &s},
    {"handle", CC_str, &h},
    {"id", CC_i32, &i},
  };

  struct ccr *ccr;
  if (ccr_init(ring, 100, CCR_DROP|CCR_OVERWRITE|CCR_CASTFILE, ccfile) < 0) goto done;
  ccr = ccr_open(ring, CCR_WRONLY);
  if (ccr == NULL) goto done;
  rc = ccr_mapv(ccr, map, adim(map));
  if (rc < 0) goto done;

  s = "hello";
  h = "world",
  i = 42;
  if (ccr_capture(ccr) < 0) printf("error\n");

  s = "liquid";
  h = "wave",
  i = 99;
  if (ccr_capture(ccr) < 0) printf("error\n");

  printf("closing\n");
  ccr_close(ccr);

  /************************************************************************
   * read the data back out
   ***********************************************************************/

  s = NULL;
  h = NULL;
  i = 0;
  ccr = ccr_open(ring, CCR_RDONLY);
  if (ccr == NULL) goto done;

  char *buf;
  size_t len;

  /* read a flat buffer */
  if (ccr_getnext(ccr, CCR_BUFFER, &buf, &len) < 0) goto done;
  hexdump(buf, len);

  /* dissect */
  printf("dissect\n");
  rc = ccr_dissect(ccr, &dis, &dislen, buf, len, 0);
  if (rc < 0) goto done;
  for(i=0; i < dislen; i++) {
    printf("%d %s %ld %s ", i, dis[i].name,
     (long)((char*)dis[i].addr - buf), 
     cc_types[ dis[i].type ]);
     if (dis[i].type == CC_i32) {
       memcpy(&i32, dis[i].addr, sizeof(i32));
       printf("%d\n", i32);
     }
     if (dis[i].type == CC_str) {
       memcpy(&i32, dis[i].addr, sizeof(i32));
       printf("%.*s\n", (int)i32, (char*)dis[i].addr + sizeof(i32));
     }
  }

  printf("closing\n");
  ccr_close(ccr);

 done:
  return rc;
}
