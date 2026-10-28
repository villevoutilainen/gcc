// D4324: an assertion's condition, outlined into its own function under
// control objects, knows what its check site knew about the object a
// reference it captures is bound to -- here from the function's
// precondition -- but not once a store may have changed it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -fcontract-analysis-engine=gimple" }

#include <contracts>
using namespace std::contracts;
int f (int x) pre<conveyor_assert_v> (x > 0);
int g ();
int body (int &r) pre<conveyor_assert_v> (r > 0)
{
  return f (r);
}
void in_assert (int &r) pre<conveyor_assert_v> (r > 0)
{
  contract_assert (f (r) > 0);
}
void stored (int &r) pre<conveyor_assert_v> (r > 0)
{
  r = g ();
  contract_assert (f (r) > 0);	// { dg-warning "cannot verify" }
}
