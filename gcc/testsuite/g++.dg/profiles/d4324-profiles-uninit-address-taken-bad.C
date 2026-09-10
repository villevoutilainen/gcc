// P4222 Initialization profile, GIMPLE checker: a [[uninit]] local
// whose address is taken anywhere in the function, other than a
// direct write or a recognized [[must_init]] call argument, is not
// verifiable -- an unverifiable [[uninit]] is a hard error, not a
// silently accepted gap (see init-profile-gimple.cc's own top
// comment).  take_ptr's own plain (unflavored) parameter would also
// trip the separate call-flavor-consistency check (Phase 3), but that
// check is skipped for a direct &x argument -- the address-escape
// check above already, unconditionally, diagnoses this exact
// occurrence, with a provably identical firing condition (see
// ip_arg_is_direct_addr_expr_p's own comment) -- so only one
// diagnostic fires, not two.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void take_ptr (int *);

void f ()
{
  [[uninit]] int x;
  take_ptr (&x); // { dg-error "before it is provably initialized" }
  x = 5;
}
