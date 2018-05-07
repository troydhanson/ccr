#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "ccr.h"
#include "sconf.h"
#include <librdkafka/rdkafka.h>

#define adim(x) (sizeof(x)/sizeof(*x))

struct mod_data {
  char *broker;
  char *topic;
  int pretty;
  int nmsgs;

  /* kafka */
  rd_kafka_t *k;
  rd_kafka_topic_t *t;
  rd_kafka_conf_t *conf;
  rd_kafka_topic_conf_t *topic_conf;
};

static void err_cb (rd_kafka_t *rk, int err, const char *reason, void *opaque) {
  struct modccr *m = (struct modccr *)opaque;
  fprintf(stderr,"librdkafka: error, %s %s: %s\n",
    rd_kafka_name(rk), rd_kafka_err2str(err), reason);
}

/* delivery report callback gets invoked for every message */
static void delivery_report_cb (rd_kafka_t *rk, void *payload, size_t len,
     rd_kafka_resp_err_t err, void *opaque, void *msg_opaque) {
  struct modccr *m = (struct modccr *)opaque;
  if ((m->verbose == 0) && (err == 0)) return;
  fprintf(stderr,"librdkafka message delivery report: %s: msg len %zu: %s\n",
    rd_kafka_name(rk), len, err ? rd_kafka_err2str(err) : "ok");
}

static int setup_kafka(struct modccr *m) {
  struct mod_data *md = (struct mod_data*)m->data;
  char err[512];
  int rc=-1, kr;

  md->conf = rd_kafka_conf_new();
  rd_kafka_conf_set_opaque(md->conf, m);
  rd_kafka_conf_set_error_cb(md->conf, err_cb);
  rd_kafka_conf_set_dr_cb(md->conf, delivery_report_cb);
  md->topic_conf = rd_kafka_topic_conf_new();


  md->k = rd_kafka_new(RD_KAFKA_PRODUCER, md->conf, err, sizeof(err));
  if (md->k == NULL) {
    fprintf(stderr, "rd_kafka_new: %s\n", err);
    goto done;
  }

  if (rd_kafka_brokers_add(md->k, md->broker) < 1) {
    fprintf(stderr, "error adding broker %s\n", md->broker);
    goto done;
  }

  md->t = rd_kafka_topic_new(md->k, md->topic, md->topic_conf);
  if (md->t == NULL) {
    fprintf(stderr, "error creating topic %s\n", md->topic);
    goto done;
  }

  rc = 0;

 done:
  return rc;
}

/* function called at 1 hz from ccr-tool. */
static int mod_periodic(struct modccr *m) {
  struct mod_data *md = (struct mod_data*)m->data;
  int n;

  /* invoke callbacks, draining */
  do { n = rd_kafka_poll(md->k, 0); }
    while (n > 0);

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
  struct mod_data *md = (struct mod_data*)m->data;
  int sc, fl, rc = -1;
  const char *err;
  char *out;
  size_t len;

  if (m->verbose) fprintf(stderr, "mod_work\n");

  fl = CCR_BUFFER | CCR_JSON;
  fl |= md->pretty ? CCR_PRETTY : 0;
  sc = ccr_getnext(ccr, fl, &out, &len);
  if (sc < 0) goto done;
  if (sc == 0) {
    rc = 0;
    goto done;
  }

  /* publish one message */
  if (m->verbose) printf("%.*s\n", (int)len, out);
  sc = rd_kafka_produce(md->t, RD_KAFKA_PARTITION_UA, 
       RD_KAFKA_MSG_F_COPY, out, len, NULL, 0, NULL);
  if (sc < 0) {
    err = rd_kafka_err2str( rd_kafka_last_error());
    fprintf(stderr, "rd_kafka_produce: %s\n", err);
    goto done;
  }

  md->nmsgs++;

  /* cause librdkafka to invoke pending cb's */
  if ((md->nmsgs % 1000) == 0)
    rd_kafka_poll(md->k, 0);

  rc = 0;

 done:
  return rc;
}

void mod_usage(void) {
  fprintf(stderr, "broker=<broker>,topic=<topic>,pretty=[0|1]\n");
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
  int pretty;
  size_t pretty_opt;

  struct sconf sc[] = {
    {.name = "broker", .type = sconf_str, .value = &broker,.vlen = &broker_len},
    {.name = "topic",  .type = sconf_str, .value = &topic, .vlen = &topic_len },
    {.name = "pretty", .type = sconf_int, .value = &pretty,.vlen = &pretty_opt},
  };

  if (m->opts == NULL) goto done;
  if (sconf( m->opts, strlen(m->opts), sc, adim(sc)) < 0) goto done;
  if (topic == NULL) goto done;
  if (broker == NULL) goto done;
  if ( (md->topic = strndup(topic, topic_len)) == NULL) goto done;
  if ( (md->broker = strndup(broker, broker_len)) == NULL) goto done;
  md->pretty = pretty_opt ? pretty : 0;

  /* initialize connections */
  if (m->verbose) fprintf(stderr,"publishing to %s/%s\n", md->broker, md->topic);
  if (setup_kafka(m) < 0) goto done;

  rc = 0;

 done:
  return rc;
}
