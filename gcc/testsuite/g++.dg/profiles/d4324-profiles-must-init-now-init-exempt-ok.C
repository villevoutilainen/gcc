// Regression guard for ip_must_init_body_check_exempt_p: this new
// check must not flag std::now_init/std::now_init_in_place's own
// definitions (<utility>) -- both are deliberately pure identity
// pass-throughs, asserting that initialization already happened by
// some means this pass can't see, and never themselves write to
// their [[must_init]] parameter.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];
#include <utility>

int use_pointer ()
{
  [[uninit]] int x;
  int *p = std::now_init (&x);
  return *p;
}

int use_in_place ()
{
  [[uninit]] int x;
  int &r = std::now_init_in_place (x);
  return r;
}
