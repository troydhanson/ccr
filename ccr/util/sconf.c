#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "sconf.h"

static int lookup_key(struct sconf *sconf, size_t nconf,
                      char *k, size_t klen, size_t *i) {

  for(*i = 0; *i < nconf; (*i)++) {
    if ((memcmp(k, sconf[*i].name, klen) == 0) &&
        (klen == strlen(sconf[*i].name))) return 0;
  }

  return -1;
}

/* 
 * the input string is like 
 * key=value,key2=value2,...
 *
 * on return, the sconf has its value and vlen set.
 *
 * if the sconf names a key which is absent in the input string,
 * this is identifiable because its *vlen is 0 when sconf returns.
 * 
 * string (sconf_str) types:
 *   when present in the input string, upon return from sconf:
 *     *value is a caller char* which has been set pointing into str
 *     *vlen must be observed as the byte length of value
 * 
 * integer (sconf_int) types:
 *   when present in the input string, upon return from sconf:
 *     *value is a caller integer which has been populated
 *     *vlen is sizeof(integer)
 */
int sconf(char *str, size_t len, struct sconf *sconf, size_t nconf) {
  char *k, *eol = str + len, *comma, *equal, tmp[20], *v;
  struct sconf *s = sconf;
  size_t klen, vlen, i;
  int rc = -1, j;

  /* initial value lengths to zero. afterward, caller can infer that
   * those keys with unchanged vlens (still zero) were unspecified */
  for(i=0; i < nconf; i++) *(s[i].vlen) = 0;

  for(k = str; k < eol; k = comma+1) {

    /* find comma that delimits key=value,key=value,... */
    comma = strchr(k,',');
    if (comma == NULL) comma = eol; /* implicit delimiter at end of line */
    equal = strchr(k,'=');
    if (equal == NULL || (equal > comma)) goto done;

    v = equal + 1;
    klen = equal - k;         /* key length, without nul */
    vlen = comma - equal - 1; /* val length, without nul */
    if (klen == 0) goto done; /* empty key? */
    if (vlen == 0) goto done; /* empty value? */

    /* find key in sconf. skip and ignore unknown keys! */
    if (lookup_key(sconf, nconf, k, klen, &i) < 0) continue;

    /* we have a key match on sconf[i]. parse the value */
    struct sconf *s = &sconf[i];
    switch(s->type) {

      case sconf_int:
        if (vlen+1 > sizeof(tmp)) goto done;
        memcpy(tmp, v, vlen);
        tmp[vlen] = '\0';
        if (sscanf(tmp, "%d", &j) != 1) goto done;
        *(int*)(s->value) = j;
        *(s->vlen) = sizeof(int);
        break;

      case sconf_str:
        *(char**)(s->value) = v;
        *(s->vlen) = vlen;
        break;

      default:
        assert(0);
        goto done;
        break;
    }
  }

  rc = 0;

 done:
  //if (rc < 0) fprintf(stderr, "syntax error in %.*s\n", (int)len, str);
  return rc;
}

