// D4324: the GIMPLE engine decides a contract_assert's comparison of a
// floating-point value with a constant from the value's range at the
// assert, through branches and arithmetic.  A value that may be a NaN
// fails every comparison, so it is never proven.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple -fcontract-conveyor-proofs" }

#include <contracts>
using namespace std::contracts;

double
bounds (double x)
{
  if (x >= 3.0 && x <= 4.0)
    {
      contract_assert<conveyor_assert_v> (x >= 3.0 && x <= 4.0);
      contract_assert<conveyor_assert_v> (x >= 0.0);
      contract_assert<conveyor_assert_v> (x < 2.0); // { dg-error "is provably false" }
    }
  return x;
}

double
arithmetic (double x)
{
  if (x >= 1.5 && x <= 2.5)
    {
      double y = x + 1.0;
      contract_assert<conveyor_assert_v> (y >= 2.5 && y <= 3.5);
    }
  return 0;
}

double
maybe_nan (double x)
{
  contract_assert<conveyor_assert_v> (x >= 0.0); // { dg-warning "cannot verify .contract_assert. condition" }
  return x;
}
