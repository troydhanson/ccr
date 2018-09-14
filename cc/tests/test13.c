#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include "cc.h"

char *conf = __FILE__ "fg";   /* test1.c becomes test1.cfg */
#define adim(x) (sizeof(x)/sizeof(*x))

struct test {
  int8_t c;
  int16_t h;
  int32_t u;
  double d;
  char mac[6];
  uint32_t ipv4;
  struct cc_blob b;
  char *s;
};

struct test t1 = {
  .c = 1,
  .h = 2,
  .u = 3,
  .d = 4,
  .mac = {5,6,7,8,9,10},
  .ipv4 = 0x04030201,
  .b = {.len = 5,
        .buf = "\xaa\xbb\xcc\xdd\xee"},
  .s = "world",
};

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
  char *flat, *json;
  size_t len, jlen;
  int rc=-1, sc;
  struct test t;
  struct in_addr ia;

#define MAPLEN 8
  struct cc_map map[MAPLEN] = {
    { "byte",     CC_i8, &t.c }, 
    { "half",     CC_i16, &t.h }, 
    { "word",     CC_i32, &t.u }, 
    { "fraction", CC_d64, &t.d }, 
    { "ether",    CC_mac, &t.mac }, 
    { "addr",     CC_ipv4, &t.ipv4 }, 
    { "data",     CC_blob, &t.b }, 
    { "name",     CC_str, &t.s }, 
  };

  struct cc *cc;
  cc = cc_open(conf, CC_FILE);
  if (cc == NULL) goto done;

  rc = cc_mapv(cc, map, adim(map));
  if (rc < 0) goto done;

  /* populate struct and capture */
  printf("populating t\n");
  t = t1;
  t.ipv4 = inet_addr("192.168.10.16");

  printf("fields of t:\n");
  ia.s_addr = t.ipv4;
  printf("%d %d %d %f %x:%x:%x:%x:%x:%x %s\n",
   (int)t.c, (int)t.h, (int)t.u, (double)t.d, 
   (unsigned)t.mac[0], (unsigned)t.mac[1], (unsigned)t.mac[2], 
   (unsigned)t.mac[3], (unsigned)t.mac[4], (unsigned)t.mac[5],
   inet_ntoa(ia));

  printf("dumping blob of length %u pointed to from t:\n", t.b.len);
  hexdump(t.b.buf, t.b.len);

  printf("printing string pointed to from t:\n");
  printf("%s\n", t.s ? t.s : "(null)");

  /* capture */
  sc = cc_capture(cc, &flat, &len);
  if (sc < 0) goto done;

  /* examine flat buffer and its json */
  printf("flattened from capturing t:\n");
  hexdump(flat, len);
  sc = cc_to_json(cc, &json, &jlen, flat, len, 0);
  if (sc < 0) goto done;
  printf("\n%.*s\n", (int)jlen, json);

  /* empty structure */
  printf("zeroing t:\n");
  memset(&t, 0, sizeof(t));

  printf("restoring t\n");
  sc = cc_restore(cc, flat, len, 0);
  if (sc < 0) goto done;

  printf("fields of t:\n");
  ia.s_addr = t.ipv4;
  printf("%d %d %d %f %x:%x:%x:%x:%x:%x %s\n",
   (int)t.c, (int)t.h, (int)t.u, (double)t.d, 
   (unsigned)t.mac[0], (unsigned)t.mac[1], (unsigned)t.mac[2], 
   (unsigned)t.mac[3], (unsigned)t.mac[4], (unsigned)t.mac[5],
   inet_ntoa(ia));

  printf("dumping blob of length %u pointed to from t:\n", t.b.len);
  hexdump(t.b.buf, t.b.len);

  printf("printing string pointed to from t:\n");
  printf("%s\n", t.s ? t.s : "(null)");


  cc_close(cc);

 done:
  return rc;
}
