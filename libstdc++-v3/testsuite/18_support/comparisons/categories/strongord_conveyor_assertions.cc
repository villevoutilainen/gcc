// D4324: std::strong_ordering::operator<=>(literal_zero, strong_ordering)
// is tagged conveyor; its own reversed-comparison negation of the
// category's internal tag value now uses __cmp_cat::__saturating_negate
// (see gcc/cp/contracts.cc's item 8 overflow scan, which checks
// NEGATE_EXPR) instead of a raw negation. Confirms this compiles and
// runs correctly under _GLIBCXX_CONVEYOR_ASSERTIONS, and that the
// reversed comparison still produces the right result in every case.
// { dg-options "-D_GLIBCXX_CONVEYOR_ASSERTIONS -fcontracts -fcontract-control-objects" }
// { dg-do run { target c++26 } }

#include <compare>
#include <testsuite_hooks.h>

int main()
{
  VERIFY( (0 <=> std::strong_ordering::less) == std::strong_ordering::greater );
  VERIFY( (0 <=> std::strong_ordering::equal) == std::strong_ordering::equal );
  VERIFY( (0 <=> std::strong_ordering::greater) == std::strong_ordering::less );
  return 0;
}
