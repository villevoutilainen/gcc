// D4324: analyzed_conveyor implies is_conveyor and additionally forces
// -fcontract-conveyor-proofs-equivalent analysis on for this specific
// contract, regardless of the command-line flag -- confirmed here by
// its complete *absence*: no -fcontract-conveyor-proofs anywhere in
// dg-additional-options, yet the unprovable conjunct still gets
// analyzed and warned about. Lenient: unknown warns, not errors (see
// d4324-proven-conveyor-unknown-bad.C for the strict sibling).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

int
f (int x) // x is unconstrained: no fact confirms or denies x < 30
{
  contract_assert<sc::analyzed_conveyor_v>(x < 30); // { dg-warning "cannot verify" }
  return x;
}

int main () { return f (5); }
