// D4324: ordinary, in-range calls to all five saturation arithmetic
// functions still compile and run cleanly under _GLIBCXX_CONVEYOR_
// ASSERTIONS -- saturating_add/sub/mul/cast are tagged conveyor
// unconditionally (zero UB surface: they saturate instead of ever
// invoking UB), and saturating_div is tagged conveyor via the stricter
// _GLIBCXX_CONVEYOR_PRE (its own division is only safe given its
// declared _GLIBCXX_PRECONDITION_NONZERO_DIVISOR precondition), both
// signed and unsigned instantiations.
// { dg-options "-D_GLIBCXX_CONVEYOR_ASSERTIONS -fcontracts -fcontract-control-objects" }
// { dg-do run { target c++26 } }

#include <numeric>
#include <testsuite_hooks.h>

int main()
{
  VERIFY( std::saturating_add(1, 2) == 3 );
  VERIFY( std::saturating_sub(5, 2) == 3 );
  VERIFY( std::saturating_mul(2, 3) == 6 );
  VERIFY( std::saturating_div(6, 2) == 3 );
  VERIFY( std::saturating_div(6u, 2u) == 3u );
  VERIFY( std::saturating_cast<short>(1) == short(1) );
  return 0;
}
