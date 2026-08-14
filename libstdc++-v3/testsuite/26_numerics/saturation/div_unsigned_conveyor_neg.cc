// D4324: same as div_conveyor_neg.cc, for an unsigned divisor -- the
// unsigned instantiation reaches the same 'return __x / __y;' as the
// signed one (saturating_div's own is_signed_v<_Tp> guard only covers
// the MIN/-1 overflow special case, not the division itself), so it
// needs the same coverage.
// { dg-options "-D_GLIBCXX_CONVEYOR_ASSERTIONS -fcontracts -fcontract-control-objects" }
// { dg-do run { target c++26 xfail *-*-* } }

#include <numeric>

void test01()
{
  volatile unsigned y = 0;
  (void) std::saturating_div(1u, y);
}

int main()
{
  test01();
  return 0;
}
