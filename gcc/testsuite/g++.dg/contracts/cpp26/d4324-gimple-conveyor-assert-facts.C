// D4324: a conveyor contract_assert establishes its condition for the
// statements it dominates, in the GIMPLE engine as in the AST engine:
// a nonzero divisor, an object address, a range, a bound that rules out
// overflow -- for a parameter or a local, at its value at the assert.
// A use the assert doesn't dominate gets no fact.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
using namespace std::contracts;

int divisor (int n, int x) conveyor
{
  contract_assert<never_proven_conveyor_v> (n != 0);
  return x / n;
}

int not_dominated (int n, int x) conveyor
{
  if (x > 3)
    contract_assert<never_proven_conveyor_v> (n != 0);
  return x / n; // { dg-error "divisor .n. not provably nonzero" }
}

int address (int *p) conveyor
{
  contract_assert<never_proven_conveyor_v> (std::is_object_address (p));
  return *p;
}

int unasserted_address (int *p) conveyor
{
  return *p; // { dg-error "pointer dereference of .p. not provably valid" }
}

int range (int i, int x) conveyor
{
  contract_assert<never_proven_conveyor_v> (i > 0 && i < 100);
  return x / i;
}

int local (int x) conveyor
{
  int i = x;
  contract_assert<never_proven_conveyor_v> (i != 0);
  return 7 / i;
}

int reassigned (int x) conveyor
{
  int i = x;
  contract_assert<never_proven_conveyor_v> (i != 0);
  i = x / 2;
  return 7 / i; // { dg-error "divisor .i. not provably nonzero" }
}

int bounded (int i) conveyor
{
  contract_assert<never_proven_conveyor_v> (i < 100);
  return i + 1;
}

int main () { int v = 1; return divisor (1, 2) + not_dominated (1, 2) + address (&v)
  + unasserted_address (&v) + range (1, 2) + local (1) + reassigned (2) + bounded (1); }
