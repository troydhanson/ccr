#include <stdlib.h>
#include <string.h>
#include "modccr.h"
#include "sconf.h"

#define adim(x) (sizeof(x)/sizeof(*x))

struct mod_data {
  char *broker;
  char *topic;
};

/* function called at 1 hz from ccr-tool */
static int mod_periodic(struct modccr *m) {
  if (m->verbose) fprintf(stderr, "mod_periodic\n");
  return 0;
}

static int mod_fini(struct modccr *m) {
  if (m->verbose) fprintf(stderr, "mod_fini\n");
  struct mod_data *md = (struct mod_data*)m->data;

  if (md->broker) free(md->broker);
  if (md->topic) free(md->topic);
  free(md);

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
  fprintf(stderr, "broker=<broker>,topic=<topic>\n");
}

int ccr_module_init(struct modccr *m) {
  int rc = -1;
  struct mod_data *md;

  if (m->verbose) fprintf(stderr, "mod_init\n");
  m->mod_periodic = mod_periodic;
  m->mod_fini = mod_fini;
  m->mod_work = mod_work;

  md = calloc(1, sizeof(struct mod_data));
  if (md == NULL) goto done;

  m->data = md;

  /* parse options */
  char *broker = NULL;
  char *topic = NULL;
  size_t broker_len;
  size_t topic_len;

  struct sconf sc[] = {
    {.name = "broker", .type = sconf_str, .value = &broker,.vlen = &broker_len},
    {.name = "topic",  .type = sconf_str, .value = &topic, .vlen = &topic_len },
  };

  if (m->opts == NULL) goto done;
  if (sconf( m->opts, strlen(m->opts), sc, adim(sc)) < 0) goto done;
  if (topic == NULL) goto done;
  if (broker == NULL) goto done;
  if ( (md->topic = strndup(topic, topic_len)) == NULL) goto done;
  if ( (md->broker = strndup(broker, broker_len)) == NULL) goto done;

  /* initialize connections */
  if (m->verbose) fprintf(stderr,"publishing to %s/%s\n", md->broker, md->topic);

  rc = 0;

 done:
  return rc;
}
