#include <stdio.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "ccr.h"

char *ccfile = __FILE__ "fg";   /* test1.c becomes test1.cfg */
char *ring = __FILE__ ".ring";  /* test1.c becomes test1.c.ring */
#define adim(x) (sizeof(x)/sizeof(*x))

int wait_for_io(int fd){ 
  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(fd,&rfds);
  struct timeval to = {.tv_sec = 5, .tv_usec=0};
  int n = select(fd+1, &rfds, NULL, NULL, &to);
  if (n <= 0) fprintf(stderr,"select %s\n", n ? "error" : "timeout");
  return n;
}

int main() {
  int rc=-1;
  int32_t i;
  char *s,*h;
  struct cc_map map[] = {
    {"name", CC_str, &s},
    {"handle", CC_str, &h},
    {"id", CC_i32, &i},
  };
  char *buf;
  size_t len;

  struct ccr *ccr;
  if (ccr_init(ring, 100, CCR_DROP|CCR_OVERWRITE|CCR_CASTFILE, ccfile) < 0) goto done;

  if (fork() > 0) { /* writer == parent */

    /************************************************************************
     * write data in 
     ***********************************************************************/
    ccr = ccr_open(ring, CCR_WRONLY);
    if (ccr == NULL) goto done;
    rc = ccr_mapv(ccr, map, adim(map));
    if (rc < 0) goto done;

    sleep(1);

    s = "hello";
    h = "world",
    i = 42;
    if (ccr_capture(ccr) < 0) printf("error\n");

    sleep(1);

    s = "liquid";
    h = "wave",
    i = 99;
    if (ccr_capture(ccr) < 0) printf("error\n");

    ccr_close(ccr);
    wait(NULL);

  } else { /* reader == child */

    /************************************************************************
     * read the data out
     ***********************************************************************/
    ccr = ccr_open(ring, CCR_RDONLY|CCR_NONBLOCK);
    if (ccr == NULL) goto done;

    int fd = ccr_get_selectable_fd(ccr);
    if (fd < 0) goto done;

    if (wait_for_io(fd) <= 0) goto done;

    /* read a json buffer */
    if (ccr_getnext(ccr, CCR_BUFFER|CCR_JSON, &buf, &len) < 0) goto done;
    printf("%.*s\n", (int)len, buf);

    if (wait_for_io(fd) <= 0) goto done;

    /* read a json buffer */
    if (ccr_getnext(ccr, CCR_BUFFER|CCR_JSON, &buf, &len) < 0) goto done;
    printf("%.*s\n", (int)len, buf);

    printf("closing\n");
    ccr_close(ccr);
  }

 done:
  return rc;
}
