// P4222 Initialization profile: std::now_init() (<utility>) asserts
// that previously-[[ref_to_uninit]] memory has been initialized by
// some means the checker can't itself see.  An ordinary function, not
// compiler magic: [[must_init]]'s own caller-trusts-the-callee's-
// declared-postcondition semantics are exactly what's needed (see
// <utility>'s own definition).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];
#include <utility>

int use_it ()
{
  int x [[uninit]];
  int *p = std::now_init (&x);
  return *p;
}
