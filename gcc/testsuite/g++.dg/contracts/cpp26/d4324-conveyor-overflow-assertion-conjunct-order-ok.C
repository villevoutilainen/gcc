// D4324/P2680 item 8's overflow scan: the same left-to-right conjunct
// refinement as the precondition sibling test, for a contract_assert's
// own condition instead.
//
// Uses 'x + x', not the original 'x++' -- see the precondition sibling
// test's own updated comment for why.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

int f (int x) conveyor
{
  contract_assert<std::contracts::conveyor_assert_v>(x < 100000 && x + x < 2048);
  return 0;
}

int main () { return f (1); }
