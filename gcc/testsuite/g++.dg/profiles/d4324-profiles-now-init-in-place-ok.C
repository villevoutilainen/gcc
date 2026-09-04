// P4222 Initialization profile: std::now_init_in_place() (<utility>)
// asserts that the *named object* itself is now initialized -- the
// object-identity counterpart to std::now_init(), which only asserts
// this of whatever a given pointer happens to point to.  Direct mirror
// of d4324-profiles-uninit-now-init-ok.C's own now_init() example, via
// a reference parameter instead of a pointer one: [[must_init]] is now
// allowed on REFERENCE_TYPE parameters as well as POINTER_TYPE ones,
// since a reference lowers to the identical ADDR_EXPR-of-the-argument
// GIMPLE call shape a pointer argument does.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];
#include <utility>

int use_it ()
{
  int x [[uninit]];
  int &r = std::now_init_in_place (x);
  return r;
}
