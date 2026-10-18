// D4324: the GIMPLE engine checks a conveyor contract_assert's own
// condition against the facts at it, like the AST engine: a provably
// false conjunct is an error, an unproven one a warning, and an
// is_object_address conjunct must be provable.  A never_proven assert
// is trusted, and an assert whose check failed establishes nothing.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple -fcontract-conveyor-proofs" }

#include <contracts>
using namespace std::contracts;

struct M { int v; };

int branch (int x) conveyor
{
  if (x > 10 && x < 20)
    {
      contract_assert<conveyor_assert_v> (x > 5);
      contract_assert<conveyor_assert_v> (x < 3); // { dg-error "condition .\\(x < 3\\). is provably false" }
      // { dg-message "established in \\\[11, 19\\\]" "" { target *-*-* } .-1 }
      contract_assert<conveyor_assert_v> (x < 15); // { dg-warning "cannot verify .contract_assert. condition .\\(x < 15\\)." }
    }
  return 0;
}

int asserted (int x) conveyor
{
  contract_assert<never_proven_conveyor_v> (x > 0);
  contract_assert<conveyor_assert_v> (x >= 1);
  contract_assert<conveyor_assert_v> (x == 0); // { dg-error "condition .\\(x == 0\\). is provably false" }
  return 0;
}

int field (M &m) conveyor
{
  contract_assert<never_proven_conveyor_v> (m.v > 4);
  contract_assert<conveyor_assert_v> (m.v > 2);
  contract_assert<conveyor_assert_v> (m.v < 1); // { dg-error "is provably false" }
  return 0;
}

int address (int *p) conveyor
{
  contract_assert<conveyor_assert_v> (std::is_object_address (p)); // { dg-error "cannot prove .is_object_address. for .p." }
  return 0;
}

int trusted_address (int *p) conveyor
{
  contract_assert<never_proven_conveyor_v> (std::is_object_address (p));
  contract_assert<conveyor_assert_v> (std::is_object_address (p));
  return *p;
}
