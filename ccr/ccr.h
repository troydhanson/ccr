#ifndef CCR_H_
#define CCR_H_
#include <stdio.h>
#include "shr.h"
#include "cc.h"

#define CCR_RDONLY    (1U << 0)
#define CCR_WRONLY    (1U << 1)
#define CCR_BUFFER    (1U << 2)
#define CCR_DROP      (1U << 3)
#define CCR_KEEPEXIST (1U << 4)
#define CCR_OVERWRITE (1U << 5)

struct ccr; /* defined internally */

int ccr_init(char *ring, size_t sz, int flags);
int ccr_stat(struct ccr *ccr);

struct ccr *ccr_open(char *ccfile, char *ring, int flags, ...);
int ccr_mapv(struct ccr *ccr, struct cc_map *map, int count);
int ccr_getnext(struct ccr *ccr, int flags, ...);
int ccr_capture(struct ccr *ccr);
int ccr_close(struct ccr *ccr);


#endif

