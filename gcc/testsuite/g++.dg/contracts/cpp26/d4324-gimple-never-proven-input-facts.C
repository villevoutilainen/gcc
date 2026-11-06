// D4324: d4324-never-proven-input-facts.C under the GIMPLE engine.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>

namespace sc = std::contracts;

int div0 (int x, int y)
{
  contract_assert<sc::never_proven_conveyor_v>(x / y > 0);	// { dg-error "divisor 'y' not provably nonzero" }
  return 0;
}

int ovf (int x)
{
  contract_assert<sc::never_proven_conveyor_v>(x + 1 > 0);	// { dg-error "not provably free of overflow" }
  return 0;
}

int deref (int *p)
{
  contract_assert<sc::never_proven_conveyor_v>(*p > 0);	// { dg-error "pointer dereference of 'p' not provably valid" }
  return 0;
}

// Established without proof, then used.
int establish_then_use (int x)
{
  contract_assert<sc::never_proven_conveyor_v>(x != 0);
  contract_assert<sc::never_proven_conveyor_v>(x > -1);
  return 10 / x;
}

int guarded (int x, int y)
{
  if (y > 0)
    contract_assert<sc::never_proven_conveyor_v>(x / y >= 0);
  return 0;
}
