// D4324: a never-proven assertion establishes its claim as a fact without
// proof, but the input facts of establishing it must be proven: its
// predicate's evaluation is checked as any conveyor predicate's -- a
// nonzero divisor, no overflow, a valid dereference (P4445: a never-proven
// predicate divided by zero while being "still checked to be UB-free").
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

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
