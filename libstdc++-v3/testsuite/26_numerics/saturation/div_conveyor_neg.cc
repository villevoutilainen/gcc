// D4324: same shape as 23_containers/array/debug/square_brackets_
// operator_conveyor_neg.cc -- routing saturating_div's own nonzero-
// divisor check through the stricter, conveyor-flavored control object
// (via its own declared _GLIBCXX_PRECONDITION_NONZERO_DIVISOR
// precondition instead of a body-internal __glibcxx_assert) still
// traps the same way at runtime under _GLIBCXX_CONVEYOR_ASSERTIONS.
// See div_unsigned_conveyor_neg.cc for the unsigned divisor sibling.
// { dg-options "-D_GLIBCXX_CONVEYOR_ASSERTIONS -fcontracts -fcontract-control-objects" }
// { dg-do run { target c++26 xfail *-*-* } }

#include <numeric>

void test01()
{
  volatile int y = 0;
  (void) std::saturating_div(1, y);
}

int main()
{
  test01();
  return 0;
}
