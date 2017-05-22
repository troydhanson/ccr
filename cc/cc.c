#include "cc-internal.h"

static int slurp(char *file, char **text, size_t *len) {
  int fd=-1, rc=-1;
  struct stat s;
  ssize_t nr;

  *text=NULL;
  *len = 0;

  if (stat(file, &s) == -1) {
    fprintf(stderr,"can't stat %s: %s", file, strerror(errno));
    goto done;
  }

  *len = s.st_size;
  if (*len == 0) { /* special case, empty file */
    rc=0;
    goto done;
  }

  fd = open(file, O_RDONLY);
  if (fd == -1) {
    fprintf(stderr,"can't open %s: %s", file, strerror(errno));
    goto done;
  }

  *text = malloc(*len);
  if (*text == NULL) {
    fprintf(stderr,"out of memory\n");
    goto done;
  }

  nr = read(fd, *text, *len);
  if (nr < 0) {
    fprintf(stderr,"read failed: %s", strerror(errno));
    goto done;
  }

  rc = 0;

 done:
  if ((rc < 0) && *text) { free(*text); *text=NULL; }
  if (fd != -1) close(fd);
  return rc;
}

static int parse_cc(struct cc *cc, char *text, size_t len) {
  int rc = -1, label_len;
  char *line, *label;
  char *sp,*nl,*def, *c, *next_line;
  unsigned t;

  /* parse the type specifier, label and optional default */
  for(line = text; line; line = next_line) {

    sp = NULL;
    nl = NULL;
    next_line = NULL;

    for(c = line; c < text + len; c++) {
      if (*c == ' ' ) { if (!sp) sp = c; }
      if (*c == '\n') { nl = c; break; };
    }

    if ((nl == NULL) || (sp == NULL)) {
      int len_remaining = len - (line - text);
      fprintf(stderr,"syntax error in [%.*s]\n", len_remaining, line);
      goto done;
    }

    next_line = (nl + 1 < text + len) ? (nl + 1) : NULL;

    for(t=0; t<NUM_TYPES; t++) {
      if(!strncmp(cc_types[t],line,sp-line)) break;
    }

    if (t >= NUM_TYPES){
      fprintf(stderr,"unknown type %s\n",line); 
      goto done;
    }

    label = sp+1;
    label_len = 0;
    def = NULL;

    for(c = label; c != nl; c++) {
      if (*c == ' ') {
        def = c + 1;
        break;
      } else label_len++;
    }

    /* names */
    utstring_clear(&cc->tmp);
    utstring_bincpy(&cc->tmp, label, label_len);
    utvector_push(&cc->names, &cc->tmp);

    /* types */
    utvector_push(&cc->output_types, &t);

    /* defaults */
    utstring_clear(&cc->tmp);
    if (def) utstring_bincpy(&cc->tmp, def, nl-def);
    utvector_push(&cc->defaults, &cc->tmp);

    /* maps */
    utvector_extend(&cc->caller_addrs);
    utvector_extend(&cc->caller_types);
  }

  rc = 0;

 done:
  return rc;
}

/* open the cc file describing the buffer format */
struct cc * cc_open( char *file_or_text, int flags, ...) {
  char *text=NULL, *file;
  int rc = -1, need_free=0;
  struct cc *cc = NULL;
  size_t len;

  va_list ap;
  va_start(ap, flags);

  /* only allow one mode or the other */
  if (((flags & CC_FILE) ^ (flags & CC_BUFFER)) == 0) {
    fprintf(stderr,"cc_open: invalid flags\n");
    goto done;
  }

  cc = calloc(1, sizeof(*cc));
  if (cc == NULL) {
    fprintf(stderr,"cc_open: out of memory\n");
    goto done;
  }
  utmm_init(&cc_mm,cc,1);

  if (flags & CC_FILE) {
    file = file_or_text;
    if (slurp(file, &text, &len) < 0) goto done;
    need_free = 1;
  }

  if (flags & CC_BUFFER) {
    text = file_or_text;
    len = va_arg(ap, size_t);
  }

  if (parse_cc(cc, text, len) < 0) goto done;

  rc = 0;

 done:

  if (text && need_free) free(text);
  if ((rc < 0) && cc) {
    utmm_fini(&cc_mm,cc,1);
    free(cc);
    cc = NULL;
  }

  va_end(ap);
  return cc;
}

int cc_close(struct cc *cc) {
  utmm_fini(&cc_mm,cc,1);
  free(cc);
  return 0;
}

/* get the slot index for the field having given name */
static int get_index(struct cc *cc, char *name) {
  int i=0;
  UT_string *s = NULL;
  while ( (s = utvector_next(&cc->names, s))) {
    if (strcmp(name, utstring_body(s)) == 0) break;
    i++;
  }
  return s ? i : -1;
}

/* associate pointers into caller memory with cc fields */
int cc_mapv(struct cc *cc, struct cc_map *map, int count) {
  int rc=-1, i, n, nmapped=0;
  struct cc_map *m;
  cc_type *ot, *ct;
  void **mp;

  for(n=0; n < count; n++) {

    m = &map[n];

    i = get_index(cc,m->name);
    if (i < 0) {
      m->addr = NULL; /* ignore field; inform caller */
      continue;
    }

    mp = utvector_elt(&cc->caller_addrs, i);
    ot = utvector_elt(&cc->output_types, i);
    ct = utvector_elt(&cc->caller_types, i);

    *ct = m->type;
    *mp = m->addr;

    if (cc_conversions[*ct][*ot] == NULL) goto done;
    nmapped++;
  }

  rc = 0;

 done:
  return (rc < 0) ? rc : nmapped;
}

int cc_capture(struct cc *cc, char **out, size_t *len) {
  int rc = -1, i=0;
  UT_string *df, *fn;
  cc_type *ot, *ct, t;
  void **mp, *p;
  char *def, *n;

  utstring_clear(&cc->flat);

  *out = NULL;
  *len = 0;

  fn = NULL;
  while( (fn = utvector_next(&cc->names, fn))) {

    mp = utvector_elt(&cc->caller_addrs, i);
    ot = utvector_elt(&cc->output_types, i);
    ct = utvector_elt(&cc->caller_types, i);
    df = utvector_elt(&cc->defaults, i);
    i++;

    def = utstring_body(df);
    n = utstring_body(fn);
    t = *ct;
    p = *mp;

    if (p == NULL) { /* no caller pointer for this field */

      if (utstring_len(df) > 0) { /* use default */
        t = CC_str;
        p = &def;
      } else {
        fprintf(stderr, "required field absent: %s\n", n);
        goto done;
      }
    }

    xcpf fcn = cc_conversions[t][*ot];
    if ((fcn == NULL) || (fcn(&cc->flat, p) < 0)) {
      fprintf(stderr,"conversion error (%s)\n", n);
      goto done;
    }
  }

  *out = utstring_body(&cc->flat);
  *len = utstring_len(&cc->flat);

  rc = 0;

 done:
  return rc;
}

/*
 * convert a flat buffer to JSON 
 * output is volatile - immediately upon return, or copy
 *
 */
int cc_to_json(struct cc *cc, char **out, size_t *out_len,
       char *in, size_t in_len, int flags) {

  int rc = -1, i, u, json_flags=0;
  char *key, *f, *o;
  UT_string *fn;
  cc_type *ot;
  json_t *j;
  size_t l;

  f = in;
  l = in_len;

  /* iterate over the cast format to interpret buffer.
   * convert each slot to json, adding to json object */
  i = 0;
  fn = NULL;
  json_object_clear(cc->json);

  while ( (fn = utvector_next(&cc->names, fn))) {
    key = utstring_body(fn);
    ot = utvector_elt(&cc->output_types, i);
    u = slot_to_json(*ot,f,l,&j);
    if (u < 0) goto done;
    if (json_object_set_new(cc->json, key, j) < 0) goto done;
    f += u;
    l -= u;
    i++;
  }

  /* dump resulting json into temp output buffer */
  utstring_clear(&cc->tmp);
  json_flags |= JSON_SORT_KEYS;
  json_flags |= (flags & CC_PRETTY) ? JSON_INDENT(1) : 0;

#if JANSSON_VERSION_HEX >= 0x021000
 int failsafe = 0;
 tryagain:
  *out = utstring_body(&cc->tmp);
  *out_len = json_dumpb(cc->json, *out, cc->tmp.n, json_flags);
  if (*out_len > cc->tmp.n) {
    utstring_reserve(&cc->tmp,*out_len);
    if (failsafe++) goto done;
    goto tryagain;
  }
#else
  /* older jansson allocates */
  o = json_dumps(cc->json, json_flags);
  if (o == NULL) goto done;
  *out_len = strlen(o);
  utstring_bincpy(&cc->tmp, o, *out_len);
  *out = utstring_body(&cc->tmp);
  free(o);
#endif

  rc = 0;

 done:
  return rc;
}

#if 0
int cc_restore(struct cc *cc, char *flat, size_t len) {
  int rc = -1;

  rc = 0;

 done:
  return rc;
}
#endif
