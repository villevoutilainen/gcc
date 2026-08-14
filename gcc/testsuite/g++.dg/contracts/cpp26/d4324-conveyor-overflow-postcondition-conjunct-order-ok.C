// D4324/P2680 item 8's overflow scan: the same left-to-right conjunct
// refinement for a postcondition's own condition -- 'x > 0' (first
// conjunct) proves the divisor nonzero for '10 / x' (second). Uses a
// div/mod-shaped pair rather than an increment: a postcondition's own
// parameter references are const, so 'x++' is never valid there
// regardless of this fix.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

int f (const int x)
post<std::contracts::conveyor_assert_v>(r: x > 0 && 10 / x > 0)
{ return x; }

int main () { return f (5) - 5; }
