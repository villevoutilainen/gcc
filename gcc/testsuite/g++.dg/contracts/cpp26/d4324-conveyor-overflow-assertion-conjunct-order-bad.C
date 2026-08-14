// D4324/P2680 item 8's overflow scan: the mirror-image conjunct order for
// a contract_assert, still correctly rejected.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

int f (int x) conveyor
{
  contract_assert<std::contracts::conveyor_assert_v>(x++ < 2048 && x < 100000); // { dg-error "increment of .x. not provably free of overflow" }
  return x;
}

int main () { return f (1); }
