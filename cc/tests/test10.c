#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>
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
  int rc=-1, sc, i, dislen;
  char *flat, *json;
  size_t len, jlen;
  struct test t;

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
  t = t1;
  t.ipv4 = inet_addr("192.168.10.16");
  sc = cc_capture(cc, &flat, &len);
  if (sc < 0) goto done;

  /* examine flat buffer and its json */
  hexdump(flat, len);
  sc = cc_to_json(cc, &json, &jlen, flat, len, 0);
  if (sc < 0) goto done;
  printf("\n%.*s\n", (int)jlen, json);

  /* dissect fields */
  struct cc_map *dismap=NULL;
  sc = cc_dissect(cc, &dismap, &dislen, flat, len, 0);
  if (sc < 0) goto done;

  assert(dismap);
  assert(dislen == MAPLEN);

  for(i = 0; i < dislen; i++) {
    printf("%d name %s off %td type %d\n", i,
      dismap[i].name,
      (ptrdiff_t)((char*)dismap[i].addr - flat),
      dismap[i].type);
  }

  cc_close(cc);

 done:
  return rc;
}
