// P4222 Initialization profile, Phase 3: [[ref_to_uninit]] flavor must
// match the pointee's [[uninit]] status in both directions -- checked
// purely at the GIMPLE level now (ip_check_assign_flavor_consistency,
// init-profile-gimple.cc); the front-end, parse-time counterpart of
// this check was removed once the GIMPLE-level one became DAA-aware
// (init-profile-gimple.cc's own comment on ip_arg_uninit_flavored_p_1),
// since running before any CFG exists meant it could never know
// whether [[uninit]] had already been cured by an earlier statement.
// x1 is already-initialized (ORDINARY), so p1's own mismatch (flavored
// destination, unflavored source) is exclusively a flavor-consistency
// finding -- no &E for the address-escape family to independently
// diagnose. x2 is genuinely still [[uninit]], so p2's mismatch
// (unflavored destination, flavored &x2 source) is instead exclusively
// an address-escape finding -- the flavor-consistency check for THIS
// direction is skipped as pure duplication for a direct &E (see
// ip_arg_is_direct_addr_expr_p's own comment).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void f ()
{
  int x1 = 7;
  int* p1 [[ref_to_uninit]] = &x1; // { dg-error "assigning a pointer not marked" }

  [[uninit]] int x2;
  int* p2 = &x2; // { dg-error "before it is provably initialized" }

  (void) p1;
  (void) p2;
}
