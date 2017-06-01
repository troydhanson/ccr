/* ccr module header */
#ifndef CCR_MODULE_H
#define CCR_MODULE_H

#include "ccr.h"

struct modccr {
  int verbose;
  int (*mod_work)(struct modccr *p, struct ccr *ccr);
  int (*mod_fini)(struct modccr *p);
  int (*mod_periodic)(struct modccr *p);
  char *opts;
  void *data;
};

#endif
