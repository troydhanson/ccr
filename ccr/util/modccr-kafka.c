#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <librdkafka/rdkafka.h>

#include "ccr.h"
#include "sconf.h"

#define adim(x) (sizeof(x)/sizeof(*x))

struct mod_data {
  char *broker;  /* kafka broker */
  char *topic;   /* topic to publish to */
  int pretty;    /* 1 to pretty-print json */
  unsigned n_pub;/* num messages published */
  unsigned n_ack;/* num messages confirmed */
  char *ofile;   /* dump kafka offset to file */

  /* latest message successfully delivered */
  int32_t partition;
  int64_t offset;

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
static void delivery_report_cb ( rd_kafka_t *rk, const rd_kafka_message_t *msg,
  void *opaque) {
  struct modccr *m = (struct modccr *)opaque;
  struct mod_data *md = (struct mod_data*)m->data;
  const char *topic;
  size_t len;

  if (msg->err != 0) {
    fprintf(stderr, "librdkafka: message delivery failure: %s\n",
      rd_kafka_err2str(msg->err));
    return;
  }

  /* successfully delivered message */
  md->n_ack++;

  /* record partition/offset from message delivery report */
  md->partition = msg->partition;
  md->offset = msg->offset;

  //rd_kafka_message_destroy(msg);
}

static int setup_kafka(struct modccr *m) {
  struct mod_data *md = (struct mod_data*)m->data;
  char err[512];
  int rc=-1, kr;

  md->conf = rd_kafka_conf_new();
  rd_kafka_conf_set_opaque(md->conf, m);
  rd_kafka_conf_set_error_cb(md->conf, err_cb);
  rd_kafka_conf_set_dr_msg_cb(md->conf, delivery_report_cb);
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

  /* request that delivery callback reports offsets */
  kr = rd_kafka_topic_conf_set(md->topic_conf, "produce.offset.report", "true",
      err, sizeof(err));
  if (kr != RD_KAFKA_CONF_OK) {
    fprintf(stderr,"rd_kafka_topic_conf_set: %s\n", err);
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
  char *report = NULL;
  int n, fd=-1, sc;
  ssize_t nr;

  /* invoke callbacks, draining */
  do { n = rd_kafka_poll(md->k, 0); } while (n > 0);

  /* periodiclly report kafka offset */
  if (md->ofile && (md->n_pub > 0)) {

    fd = open(md->ofile, O_WRONLY|O_TRUNC|O_CREAT, 0644);
    if (fd == -1) {
      fprintf(stderr, "open %s: %s\n", md->ofile, strerror(errno));
      goto done;
    }

    sc = asprintf(&report,
              "{\n"
                "\"topic\": \"%s\",\n"
                "\"partition\": %u,\n"
                "\"offset\": %zu,\n"
                "\"published\": %u,\n"
                "\"confirmed\": %u\n"
              "}\n",
              md->topic,
              (unsigned)md->partition,
              (size_t)md->offset,
              md->n_pub,
              md->n_ack);

    if (sc == -1) {
      fprintf(stderr, "asprintf: failed\n");
      goto done;
    }

    nr = write(fd, report, sc);
    if (nr < 0) {
      fprintf(stderr, "write: %s\n", strerror(errno));
      goto done;
    }
  }

 done:
  if (fd != -1) close(fd);
  if (report) free(report);
  return 0;
}

static int mod_fini(struct modccr *m) {
  if (m->verbose) fprintf(stderr, "mod_fini\n");
  struct mod_data *md = (struct mod_data*)m->data;

  if (md->broker) free(md->broker);
  if (md->topic) free(md->topic);
  if (md->ofile) free(md->ofile);
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

  /* publish one message.
   *
   * librdkafka returns an "error" if the producer queue
   * is full, which requires draining using rd_kafka_poll
   * and retrying (c.f. examples/rdkafka_simple_producer.c)
   */
 retry:
  sc = rd_kafka_produce(md->t, RD_KAFKA_PARTITION_UA, 
       RD_KAFKA_MSG_F_COPY, out, len, NULL, 0, NULL);
  if (sc < 0) {
#if RD_KAFKA_VERSION < 0x00090100
    if (errno == ENOBUFS) {
      rd_kafka_poll(md->k, 1000);
      goto retry;
    } else {
      err = rd_kafka_err2str( rd_kafka_errno2err(errno) );
      fprintf(stderr, "rd_kafka_produce: %s\n", err);
      goto done;
    }
#else
    if (rd_kafka_last_error() == RD_KAFKA_RESP_ERR__QUEUE_FULL) {
      rd_kafka_poll(md->k, 1000);
      goto retry;
    } else {
      err = rd_kafka_err2str( rd_kafka_last_error() );
      fprintf(stderr, "rd_kafka_produce: %s\n", err);
      goto done;
    }
#endif
  }

  md->n_pub++;

  /* cause librdkafka to invoke pending cb's */
  if ((md->n_pub % 100) == 0)
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
  char *ofile = NULL;
  size_t broker_len;
  size_t topic_len;
  size_t ofile_len;
  int pretty;
  size_t pretty_opt;

  struct sconf sc[] = {
    {.name = "broker", .type = sconf_str, .value = &broker,.vlen = &broker_len},
    {.name = "topic",  .type = sconf_str, .value = &topic, .vlen = &topic_len },
    {.name = "pretty", .type = sconf_int, .value = &pretty,.vlen = &pretty_opt},
    {.name = "ofile",  .type = sconf_str, .value = &ofile, .vlen = &ofile_len},
  };

  if (m->opts == NULL) goto done;
  if (sconf( m->opts, strlen(m->opts), sc, adim(sc)) < 0) goto done;
  if (topic == NULL) goto done;
  if (broker == NULL) goto done;
  if ( (md->topic = strndup(topic, topic_len)) == NULL) goto done;
  if ( (md->broker = strndup(broker, broker_len)) == NULL) goto done;
  md->pretty = pretty_opt ? pretty : 0;
  if (ofile) {
    md->ofile = strndup(ofile, ofile_len);
    if (md->ofile == NULL) goto done;
  }

  /* initialize connections */
  if (m->verbose) fprintf(stderr,"publishing to %s/%s\n", md->broker, md->topic);
  if (setup_kafka(m) < 0) goto done;

  rc = 0;

 done:
  return rc;
}
