// D4324: companion "ok" case for d4324-proven-conveyor-postcondition-
// relational-result-bad.C -- the exact same shape, but the claim is
// genuinely true and derivable from the return expression's own
// literal value, so it must be silently accepted, not flagged.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

int
always_positive ()
  post<sc::proven_conveyor_v> (r: r > 0)
{
  return 1;
}

int main () { return always_positive () > 0 ? 0 : 1; }
