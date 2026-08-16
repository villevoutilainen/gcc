// D4324: never_proven_conveyor exempts a postcondition from the new
// merged-facts check entirely, the same as it already does for
// contract_assert -- the claim below is genuinely, provably false (the
// function always returns -1), but must be silently accepted since
// never_proven means "never checked or diagnosed, still established
// for callers." Contrast d4324-proven-conveyor-postcondition-
// relational-result-bad.C, the exact same shape under proven_conveyor
// instead, where this is caught.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

int
always_negative ()
  post<sc::never_proven_conveyor_v> (r: r > 0)
{
  return -1;
}

int main () { return always_negative () < 0 ? 0 : 1; }
