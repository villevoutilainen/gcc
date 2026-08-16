// D4324: proven_conveyor is strict -- like analyzed_conveyor, it
// forces analysis on regardless of the command-line flag (no
// -fcontract-conveyor-proofs anywhere in dg-additional-options here
// either), but an unprovable conjunct is *also* a hard error, not just
// a warning, matching WG14 P4021R2's compile_assert() outcome table
// exactly (proof true, proof false, or cannot prove are the only three
// outcomes, and only the first is well-formed) -- except deterministic,
// since this runs through the same oa_* static analysis as everything
// else here, not an optimizer's own dead-code elimination.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

int
f (int x) // x is unconstrained: no fact confirms or denies x < 30
{
  contract_assert<sc::proven_conveyor_v>(x < 30); // { dg-error "cannot prove" }
  return x;
}

int main () { return f (5); }
