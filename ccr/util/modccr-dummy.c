#include "modccr.h"

/* function called at 1 hz from ccr-tool */
static int mod_periodic(struct modccr *m) {
  if (m->verbose) fprintf(stderr, "mod_periodic\n");
  return 0;
}

static int mod_fini(struct modccr *m) {
  if (m->verbose) fprintf(stderr, "mod_fini\n");
  return 0;
}

static int mod_work(struct modccr *m, struct ccr *ccr) {
  int sc, fl, rc = -1;
  char *out;
  size_t len;

  if (m->verbose) fprintf(stderr, "mod_work\n");

  fl = CCR_BUFFER | CCR_JSON | CCR_PRETTY;
  sc = ccr_getnext(ccr, fl, &out, &len);
  if (sc < 0) goto done;
  if (sc > 0) printf("%.*s\n", (int)len, out);

  rc = 0;

 done:
  return rc;
}

void mod_usage(void) {
  fprintf(stderr, "no options\n");
}

int ccr_module_init(struct modccr *m) {
  if (m->verbose) fprintf(stderr, "mod_init\n");
  m->mod_periodic = mod_periodic;
  m->mod_fini = mod_fini;
  m->mod_work = mod_work;
  return 0;
}
