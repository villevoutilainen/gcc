// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }
#include "d4324-template-contract-2.H"

// Without its never_proven precondition (lost across the PCH), tid's
// 'return x' was "reference returned from a conveyor function is not
// provably to an object", and the call needed a proof for 'r'.
int
f (int &r)
{
  return tid (r);
}

int
main ()
{
  int i = 1;
  return tid_strict (i) - 1;
}
