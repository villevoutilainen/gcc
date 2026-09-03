// P4222 Initialization profile, Phase 4c (S5.6): [[uninit]] and
// [[ref_to_uninit]] are rejected on a union member outright -- unions
// are explicitly out of scope for this profile (tracking which member
// is "active" needs runtime state this profile doesn't add).  Also a
// regression test for a real ICE: DECL_CONTEXT isn't set yet at
// attribute-processing time during member parsing, so the union check
// must consult current_class_type instead (see tree.cc's own
// ip_field_in_union_p).
// { dg-do compile { target c++11 } }

union U {
  int x [[uninit]]; // { dg-error "not supported on a union member" }
  double d;
};

union V {
  int* p [[ref_to_uninit]]; // { dg-error "not supported on a union member" }
  double d;
};
