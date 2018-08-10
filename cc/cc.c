#include "cc-internal.h"

#define x(t) #t,
char *cc_types[] = { CC_TYPES };
#undef x

static int is_type_name(char *name, size_t len) {
  unsigned int t;
  int sc;

  for(t=0; t < CC_MAX; t++) {
    sc = strncmp(cc_types[t], name, len);
    if (sc == 0) return t;
  }

  return -1;
}

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

/* 
 * find start and length of column N (one-based)
 * in input buffer buf of length buflen
 *
 * columns must be space delimited
 * returns NULL if column not found
 *
 * the final column may end in newline or eob  
 *
 * col: column index (1-based)
 * len: OUTPUT parameter (column length)
 * buf: buffer to find columns in
 * buflen: length of buf
 *
 * returns:
 *   pointer to column N, or NULL
 */
static
char *get_col(int col, size_t *len, char *buf, size_t buflen) {
  char *b, *start=NULL, *eob;
  int num;

  eob = buf + buflen;

  b = buf;
  num = 0;  /* column number */
  *len = 0; /* column length */

  while (b < eob) {

    if ((*b == ' ') && (num == col)) break; /* end of sought column */
    if (*b == '\n') break;                  /* end of line */

    if  (*b == ' ') *len = 0;               /* skip over whitespace */
    if ((*b != ' ') && (*len == 0)) {       /* record start of column */
      num++;
      start = b;
    }
    if  (*b != ' ') (*len)++;               /* increment column length */
    b++;
  }

  if ((*len) && (num == col)) return start;
  return NULL;
}

/*
 * is_empty_line
 *
 * used to skip whitespace/empty lines
 * 
 * line: buffer to scan
 * len:  buffer length (may surpass line's newline delimiter)
 *
 * returns
 *  0 (line not empty, nl points to first non-ws byte)
 *  1 (line is empty, and nl points to newline,
       or nl is NULL if buffer ended before newline)
 *
 */
static int ws[256] = { 
  ['\n'] = 2, 
  [' '] =  1, 
  ['\t'] = 1,
};
static int is_empty_line(char *line, size_t len, char **nl) {
  int wc;

  *nl = line;

  while(len--) {
    wc = ws[ (size_t)(**nl) ];
    if (wc == 0) return 0;   /* found non-ws */
    if (wc == 2) return 1;   /* found newline */
    assert(wc == 1);         /* regular ws */
    (*nl)++;
  }

  /* buffer ran out, containing only ws, no newline */
  *nl = NULL;
  return 1;
}

/*
 * parse_cc
 *
 * parse cc cast file
 * having lines of the form
 * <type> <name> [default]
 *
 * returns
 *    0 success
 *  < 0 error
 *
 */
static int parse_cc(struct cc *cc, char *buf, size_t sz) {
	char *line, *name, *type, *defult, *b;
  size_t len1, len2, len3, left;
  int lno=1, type_i;

  line = buf;
  while (line < buf+sz) {

    left = sz - (line-buf);

    if (is_empty_line(line, left, &b)) {
      if (b == NULL) break; /* empty and runs into eob */
      goto next_line;       /* empty, b is its newline */
    }

    type = get_col(1, &len1, line, left);
    type_i = type ? is_type_name(type, len1) : -1;
    name = get_col(2, &len2, line, left);
    defult = get_col(3, &len3, line, left);

    if ((type_i == -1) || (name == NULL)) {
      fprintf(stderr, "parse_cc: syntax error on line %d\n", lno);
      return -1;
    }

    /* type */
    utvector_push(&cc->output_types, &type_i);

    /* name */
    utstring_clear(&cc->tmp);
    utstring_bincpy(&cc->tmp, name, len2);
    utvector_push(&cc->names, &cc->tmp);

    /* default */
    utstring_clear(&cc->tmp);
    if (defult) utstring_bincpy(&cc->tmp, defult, len3);
    utvector_push(&cc->defaults, &cc->tmp);

    /* maps */
    utvector_extend(&cc->caller_addrs);
    utvector_extend(&cc->caller_types);
    utvector_extend(&cc->dissect_map);

    /* advance to next line */
    b = defult ? (defult+len3) : (name+len2);
    while ((b < buf+sz) && (*b != '\n')) b++;
   next_line:
    line = b+1;
    lno++;
  }

  return 0;
}

/* open the cc file describing the buffer format */
struct cc * cc_open( char *file_or_text, int flags, ...) {
  int rc = -1, need_free=0, sc;
  char *text=NULL, *file;
  struct cc *cc = NULL;
  size_t len=0;

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

  assert(len > 0);
  sc = parse_cc(cc, text, len);
  if (sc < 0) goto done;

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

static void cc_mapv_clear(struct cc *cc)
{
  void **mp = NULL;
  while ( (mp = utvector_next(&cc->caller_addrs, mp))) {
    *mp = NULL;
  }
}

/* associate pointers into caller memory with cc fields */
int cc_mapv(struct cc *cc, struct cc_map *map, int count) {
  int rc=-1, i, n, nmapped=0;
  struct cc_map *m;
  cc_type *ot, *ct;
  void **mp;

  cc_mapv_clear(cc);

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

/*
 * cc_capture
 *
 * pack caller memory from previously established cc_map
 * to flattened buffer.
 *
 * DO NOT free the output buffer (out)
 * it is internal memory associated with the struct cc
 * and is released on cc_close. it is also overwritten
 * by a subsequent call to cc_capture
 *
 * this flattened buffer can be transmitted or saved,
 * as is. it can also be dissected into its fields 
 * (cc_dissect) or dumped to json (cc_to_json).
 *
 * returns
 *  0 success
 * -1 error (such as, an unmapped field in the cc_map)
 *
 */
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
 * cc_to_json
 *
 * convert a flat buffer to JSON 
 *
 * the flat buffer is one previously obtained from
 * cc_capture
 *
 * NOTE: output is volatile internal memory! 
 *       DO NOT free! (it is freed in cc_close)
 *       use immediately, or copy.
 *       it is overwritten on the next call to cc_to_json
 *
 * flags:
 *    CC_PRETTY  pretty print JSON
 *    CC_NEWLINE append newline
 *
 * returns
 *  0 success
 * -1 error
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

  if (flags & CC_NEWLINE) {
    utstring_reserve(&cc->tmp, 1);
    utstring_bincpy(&cc->tmp, "\n", 1);
    *out = utstring_body(&cc->tmp);
    *out_len = (*out_len) + 1;
  }

  rc = 0;

 done:
  return rc;
}

int cc_restore(struct cc *cc, char *flat, size_t len) {
  int rc = -1;

  rc = 0;

 done:
  return rc;
}

/*
 * cc_dissect
 *
 * convert a flattened buffer to a list of cc_map
 *
 * given a flattened buffer (in) this function
 * parses its contents, populating a cc_map[]
 * with one element for each field:
 *
 *   map[n].name  - C string with field name
 *   map[n].addr  - pointer into input buffer
 *   map[n].type  - CC_i8, CC_i16, etc field type
 *
 * the map array is volatile; it's internal to the ccr structure.
 * it's overwritten on each call. it remains valid only as long 
 * as the caller keeps the input buffer intact until cc_dissect
 * or ccr_close is called.
 *
 *  map:    receives the map
 *  count:  receives number of elements in map
 *  in:     flattened input buffer (e.g. from cc_capture)
 *  in_len: length of in
 *  flags:  must be 0

 * returns
 *  0 success
 * -1 error (such as input buffer fails validation)
 *
 */
int cc_dissect(struct cc *cc, struct cc_map **map, int *count,
       char *in, size_t in_len, int flags) {
  struct cc_map *dm;
  int rc = -1, i;
  UT_string *fn;
  uint32_t u32;
  cc_type *ot;
  uint8_t u8;
  size_t l,r;
  char *p;

  if (flags) goto done;
  *count = utvector_len(&cc->dissect_map);
  *map = utvector_elt(&cc->dissect_map, 0);
  dm = *map;

  fn = NULL;
  p = in;
  r = in_len;

  for(i = 0; i < *count; i++) {

    fn = utvector_elt(&cc->names, i);
    ot = utvector_elt(&cc->output_types, i);

    dm[i].name = utstring_body(fn);
    dm[i].type = *ot;
    dm[i].addr = p;

    switch( dm[i].type ) {
      case CC_i8:   l = sizeof(uint8_t);  break;
      case CC_i16:  l = sizeof(int16_t);  break;
      case CC_u16:  l = sizeof(uint16_t); break;
      case CC_i32:  l = sizeof(int32_t);  break;
      case CC_d64:  l = sizeof(double);   break;
      case CC_mac:  l = 6; break;
      case CC_ipv4: l = 4; break;
      case CC_ipv46:
        if (r < sizeof(uint8_t)) goto done;
        memcpy(&u8, p, sizeof(uint8_t));
        assert((u8 == 4) || (u8 == 16));
        l = sizeof(uint8_t) + u8;
        break;
      case CC_str8:
        if (r < sizeof(uint8_t)) goto done;
        memcpy(&u8, p, sizeof(uint8_t));
        l = sizeof(uint8_t) + u8;
        break;
      case CC_str: /* FALL THRU */
      case CC_blob:
        if (r < sizeof(uint32_t)) goto done;
        memcpy(&u32, p, sizeof(uint32_t));
        l = sizeof(uint32_t) + u32;
        break;
      default:
        assert(0);
        break;
    }

    if (r < l) goto done;
    p += l;
    r -= l;
  }

  /* require input was fully consumed */
  if (r > 0) goto done;
  rc = 0;

 done:
  return rc;
}

/*
 * cc_count
 *
 * get the number of fields in the cc map
 *
 */
int cc_count(struct cc *cc) {
  int c;

  c = utvector_len(&cc->names);
  return c;
}


