// D4324: AST-engine counterpart of
// d4324-gimple-conveyor-assert-field-facts.C.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
using namespace std::contracts;

struct M
{
  int v;
  int n;

  int div (int x) conveyor
  {
    contract_assert<never_proven_conveyor_v> (n > 0);
    return x / n;
  }

  M &operator/= (const M &r) conveyor
  {
    contract_assert<never_proven_conveyor_v> (r.v != 0);
    v /= r.v;
    return *this;
  }
};

int ref_param (const M &r, int x) conveyor
{
  contract_assert<never_proven_conveyor_v> (r.v != 0);
  return x / r.v;
}

int local (int x) conveyor
{
  M m{x, 1};
  contract_assert<never_proven_conveyor_v> (m.v != 0);
  return 5 / m.v;
}

int written (M &r, int x) conveyor
{
  contract_assert<never_proven_conveyor_v> (r.v != 0);
  r.v = x;
  return x / r.v; // { dg-error "divisor \[^\n\]* not provably nonzero" }
}

void reset (M &) conveyor;

int passed (M &r, int x) conveyor
{
  contract_assert<never_proven_conveyor_v> (r.v != 0);
  reset (r);
  return x / r.v; // { dg-error "divisor \[^\n\]* not provably nonzero" }
}

int not_dominated (const M &r, int x) conveyor
{
  if (x > 1)
    contract_assert<never_proven_conveyor_v> (r.v != 0);
  return x / r.v; // { dg-error "divisor \[^\n\]* not provably nonzero" }
}
