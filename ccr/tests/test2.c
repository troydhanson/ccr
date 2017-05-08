#include <stdio.h>
#include "ccr.h"

char *ccfile = __FILE__ "fg";   /* test1.c becomes test1.cfg */
char *ring = __FILE__ ".ring";  /* test1.c becomes test1.c.ring */
#define adim(x) (sizeof(x)/sizeof(*x))


int main() {
  int rc=-1;
  char *s;
  struct cc_map map[] = {
    {"name", CC_str, &s},
  };

  struct ccr *ccr;
  if (ccr_init(ring, 100, CCR_DROP|CCR_OVERWRITE) < 0) goto done;
  ccr = ccr_open(ccfile, ring, CCR_RDONLY);
  if (ccr == NULL) goto done;
  rc = ccr_mapv(ccr, map, adim(map));
  if (rc < 0) goto done;
  printf("closing\n");
  ccr_close(ccr);

 done:
  return rc;
}
