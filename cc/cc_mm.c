#include "cc-internal.h"

const UT_mm ptr_mm = { .sz = sizeof(void*) };
const UT_mm ccmap_mm={ .sz = sizeof(struct cc_map) };

static void cc_init(void *_cc) {
  struct cc *cc = (struct cc*)_cc;
  utvector_init(&cc->names,        utstring_mm);
  utvector_init(&cc->output_types, utmm_int);
  utvector_init(&cc->defaults,     utstring_mm);
  utvector_init(&cc->caller_addrs, &ptr_mm);
  utvector_init(&cc->caller_types, utmm_int);
  utvector_init(&cc->dissect_map,  &ccmap_mm);
  utstring_init(&cc->flat);
  utstring_init(&cc->rest);
  utstring_init(&cc->tmp);
  cc->json = json_object();
}
static void cc_fini(void *_cc) {
  struct cc *cc = (struct cc*)_cc;
  utvector_fini(&cc->names);
  utvector_fini(&cc->output_types);
  utvector_fini(&cc->defaults);
  utvector_fini(&cc->caller_addrs);
  utvector_fini(&cc->caller_types);
  utvector_fini(&cc->dissect_map);
  utstring_done(&cc->flat);
  utstring_done(&cc->rest);
  utstring_done(&cc->tmp);
  json_decref(cc->json);
}
static void cc_copy(void *_dst, void *_src) {
  struct cc *dst = (struct cc*)_dst;
  struct cc *src = (struct cc*)_src;
  //utmm_copy(utvector_mm, &dst->names, &src->names, 1);
  //utmm_copy(utvector_mm, &dst->output_types, &src->output_types, 1);
  //utmm_copy(utvector_mm, &dst->defaults, &src->defaults, 1);
  //utmm_copy(utvector_mm, &dst->caller_addrs, &src->caller_addrs, 1);
  //utmm_copy(utvector_mm, &dst->caller_types, &src->caller_types, 1);
  //utmm_copy(utvector_mm, &dst->dissect_map, &src->dissect_map, 1);
  //utmm_copy(utstring_mm, &dst->flat, &src->flat, 1);
  //utmm_copy(utstring_mm, &dst->rest, &src->rest, 1);
  //utmm_copy(utstring_mm, &dst->tmp, &src->tmp, 1);
  utvector_copy(&dst->names,       &src->names);
  utvector_copy(&dst->output_types,&src->output_types);
  utvector_copy(&dst->defaults,    &src->defaults);
  utvector_copy(&dst->caller_addrs,&src->caller_addrs);
  utvector_copy(&dst->caller_types,&src->caller_types);
  utvector_copy(&dst->dissect_map, &src->dissect_map);
  utstring_bincpy(&dst->flat,utstring_body(&src->flat),utstring_len(&src->flat));
  utstring_bincpy(&dst->rest,utstring_body(&src->rest),utstring_len(&src->rest));
  utstring_bincpy(&dst->tmp,utstring_body(&src->tmp),utstring_len(&src->tmp));
  dst->json = json_incref(src->json);
}
static void cc_clear(void *_cc) {
  struct cc *cc = (struct cc*)_cc;
  utvector_clear(&cc->names);
  utvector_clear(&cc->output_types);
  utvector_clear(&cc->defaults);
  utvector_clear(&cc->caller_addrs);
  utvector_clear(&cc->caller_types);
  utvector_clear(&cc->dissect_map);
  utstring_clear(&cc->flat);
  utstring_clear(&cc->rest);
  utstring_clear(&cc->tmp);
  json_object_clear(cc->json);
}

UT_mm const cc_mm = {
  .sz = sizeof(struct cc),
  .init = cc_init,
  .fini = cc_fini,
  .copy = cc_copy,
  .clear = cc_clear,
};

