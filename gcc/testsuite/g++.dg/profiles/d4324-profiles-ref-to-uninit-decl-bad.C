// P4222 Initialization profile, Phase 3: [[ref_to_uninit]] flavor must
// match the pointee's [[uninit]] status in both directions -- checked
// purely at the GIMPLE level now (ip_check_assign_flavor_consistency,
// init-profile-gimple.cc); the front-end, parse-time counterpart of
// this check was removed once the GIMPLE-level one became DAA-aware
// (init-profile-gimple.cc's own comment on ip_arg_uninit_flavored_p_1),
// since running before any CFG exists meant it could never know
// whether [[uninit]] had already been cured by an earlier statement.
// x2's address being taken by the mismatched p2 initialization also
// makes it unverifiable, a second, independent diagnostic on the same
// line.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void f ()
{
  int x1 = 7;
  int* p1 [[ref_to_uninit]] = &x1; // { dg-error "assigning a pointer not marked" }

  [[uninit]] int x2;
  int* p2 = &x2; // { dg-error "assigning a pointer marked" }
  // { dg-error "before it is provably initialized" "" { target *-*-* } .-1 }

  (void) p1;
  (void) p2;
}
