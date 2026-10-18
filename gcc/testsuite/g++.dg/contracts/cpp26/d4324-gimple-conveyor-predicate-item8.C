// D4324: the GIMPLE engine applies item 8's checks to a conveyor
// contract's own condition, like the AST engine, against what is known
// at the check: a divisor, an array index.  A conjunct can rely on the
// ones before it.  A never_proven contract is exempt.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
using namespace std::contracts;

int divide (int x, int y) pre<conveyor_assert_v> (x / y > 1) { return x; } // { dg-error "divisor .y. not provably nonzero" }
int guarded (int x, int y) pre<conveyor_assert_v> (y != 0 && x / y > 1) { return x; }
int trusted (int x, int y) pre<never_proven_conveyor_v> (x / y > 1) { return x; }

int index (int i) conveyor
{
  int arr[4] = { 1, 2, 3, 4 };
  contract_assert<conveyor_assert_v> (arr[i] > 0); // { dg-error "array index .i. not provably in-bounds" }
  return 0;
}

int index_ok (int i) conveyor
{
  int arr[4] = { 1, 2, 3, 4 };
  if (i >= 0 && i < 4)
    contract_assert<conveyor_assert_v> (arr[i] > 0);
  return 0;
}
