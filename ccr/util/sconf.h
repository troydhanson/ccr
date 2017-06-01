#include <assert.h>
#include <stdio.h>

struct sconf {
 char *name;
 enum {sconf_int, sconf_str} type;
 void *value;
 size_t *vlen;
};

/*
 * parse string s consisting of key=value,key=value,...
 * into the sconf 
 *
 */
int sconf(char *str, size_t len, struct sconf *sconf, size_t nconf);
