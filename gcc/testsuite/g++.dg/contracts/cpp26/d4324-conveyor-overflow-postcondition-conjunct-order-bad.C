// D4324/P2680 item 8's overflow scan: the mirror-image conjunct order for
// a postcondition, still correctly rejected.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

int f (const int x)
post<std::contracts::conveyor_assert_v>(r: 10 / x > 0 && x > 0) // { dg-error "not provably nonzero" }
{ return x; }

int main () { return f (5); }
