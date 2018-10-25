#ifndef CCR_H_
#define CCR_H_
#include "shr.h"
#include "cc.h"

#define CCR_RDONLY    (1U << 0)
#define CCR_NONBLOCK  (1U << 1)
#define CCR_WRONLY    (1U << 2)
#define CCR_BUFFER    (1U << 3)
#define CCR_DROP      (1U << 4)
#define CCR_FARM      (1U << 5)
#define CCR_SYNC      (1U << 6)
#define CCR_MLOCK     (1U << 7)
#define CCR_KEEPEXIST (1U << 8)
#define CCR_OVERWRITE (1U << 9)
#define CCR_CASTFILE  (1U << 10)
#define CCR_CASTCOPY  (1U << 11)
#define CCR_CASTTEXT  (1U << 12)
#define CCR_JSON      (1U << 13)
#define CCR_PRETTY    (1U << 14)
#define CCR_NEWLINE   (1U << 15)
#define CCR_LEN4FIRST (1U << 16)
#define CCR_RESTORE   (1U << 17)

struct ccr; /* defined internally */

int ccr_init(char *ring, size_t sz, int flags, ...);
int ccr_stat(struct ccr *ccr);

struct ccr *ccr_open(char *ring, int flags, ...);
int ccr_mapv(struct ccr *ccr, struct cc_map *map, int count);
ssize_t ccr_getnext(struct ccr *ccr, int flags, ...);
int ccr_capture(struct ccr *ccr);
ssize_t ccr_flush(struct ccr *ccr, int wait);
int ccr_close(struct ccr *ccr);
int ccr_get_selectable_fd(struct ccr *ccr);
int ccr_dissect(struct ccr *ccr, struct cc_map **map, int *count,
       char *in, size_t in_len, int flags);
struct cc *ccr_get_cc(struct ccr *ccr);

/* struct specific to ccr-tool and modules it loads */
struct modccr {
  int verbose;
  int (*mod_work)(struct modccr *p, struct ccr *ccr);
  int (*mod_fini)(struct modccr *p);
  int (*mod_periodic)(struct modccr *p);
  char *opts;
  void *data;
};

#endif

