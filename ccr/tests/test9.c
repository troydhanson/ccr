#include <stdio.h>
#include "ccr.h"

char *ccfile = __FILE__ "fg";   /* test1.c becomes test1.cfg */
char *ring = __FILE__ ".ring";  /* test1.c becomes test1.c.ring */
#define adim(x) (sizeof(x)/sizeof(*x))


int main() {
  int rc=-1;
  int32_t i;
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

  printf("initializing locals\n");
  s = "hello";
  h = "world",
  i = 42;
  printf("%s %s %d\n", s?s:"(null)", h?h:"(null)", i);
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

  printf("zeroing\n");
  h=NULL;
  s=NULL;
  i=0;
  printf("%s %s %d\n", s?s:"(null)", h?h:"(null)", i);

  /* map back to our memory */
  rc = ccr_mapv(ccr, map, adim(map));
  if (rc < 0) goto done;

  /* read - restoring memory */
  if (ccr_getnext(ccr, CCR_RESTORE) < 0) goto done;

  printf("restored\n");
  printf("%s %s %d\n", s?s:"(null)", h?h:"(null)", i);
  printf("closing\n");
  ccr_close(ccr);

 done:
  return rc;
}
