// D4324: companion to d4324-proven-conveyor-postcondition-multi-
// return-unknown-bad.C -- same two-return shape, but the claim
// genuinely holds on *every* path (both branches return a positive
// value), so the merge must accept it, proving the merge doesn't
// over-invalidate just because there's more than one return.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

int
always_positive_either_way (bool cond)
  post<sc::proven_conveyor_v> (r: r > 0)
{
  if (cond)
    return 1;
  return 2;
}

int main ()
{
  return (always_positive_either_way (true) > 0
	  && always_positive_either_way (false) > 0) ? 0 : 1;
}
