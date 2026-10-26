// D4324: std::is_object_address is never evaluated at run time -- it is
// proven, or the program is ill-formed -- so a conveyor contract whose
// condition is nothing but such calls has no run-time check: no
// assertion_context, no call to its control object (std::move's and
// std::forward's contracts made a check at every use).  A condition with
// another operand is checked as ever.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -fdump-tree-gimple" }

#include <contracts>

int g (int &r) conveyor
  pre<std::contracts::never_proven_conveyor_v>
    (std::is_object_address (__builtin_addressof (r)))
{
  return r;
}

int f (int *p)
{
  contract_assert<std::contracts::never_proven_conveyor_v>
    (std::is_object_address (p));
  contract_assert<std::contracts::never_proven_conveyor_v>
    (std::is_object_address (p) && *p > 0);
  return g (*p);
}

int main ()
{
  int i = 1;
  return f (&i) == 1 ? 0 : 1;
}

// Only the second assertion's check remains.
// { dg-final { scan-tree-dump-times "never_proven_conveyor::operator\\(\\) \\(&" 1 "gimple" } }
