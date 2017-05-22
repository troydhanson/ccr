#include <stdio.h>
#include "ccr.h"

char *ccfile = __FILE__ "fg";   /* test1.c becomes test1.cfg */
char *ring = __FILE__ ".ring";  /* test1.c becomes test1.c.ring */

int main() {
  int rc=-1;

  struct ccr *ccr;
  if (ccr_init(ring, 100, CCR_DROP|CCR_OVERWRITE|CCR_CASTFILE, ccfile) < 0) goto done;
  ccr = ccr_open(ring, CCR_RDONLY);
  if (ccr == NULL) goto done;
  printf("closing\n");
  ccr_close(ccr);

 done:
  return rc;
}
