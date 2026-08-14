// D4324/P2680 item 8's overflow scan: a self-trust precondition gives x
// an established lower bound strictly above TYPE_MIN, proving '-x' safe
// via oa_get_range.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

int f (int x) conveyor
pre<std::contracts::conveyor_assert_v>(x > -100000)
{ return -x; }

int main () { return f (5) + 5; }
