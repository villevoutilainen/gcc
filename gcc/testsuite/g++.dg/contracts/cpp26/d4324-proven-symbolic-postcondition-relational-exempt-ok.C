// D4324: symbolic postconditions stay exempt from the new merged-facts
// check -- the exact same shape as d4324-proven-conveyor-postcondition-
// relational-result-bad.C (a claim that is genuinely, provably false),
// but under proven_symbolic instead of proven_conveyor. Must still
// compile cleanly: a postcondition's job is to hand callers a trusted
// fact, and for symbolic that fact is a first-class axiom the user
// vouches for outright, never independently checked -- unlike
// conveyor, whose handed-off fact is backed by the same real,
// mandatory UB-freedom substrate everything else conveyor relies on.
// This is the test that locks in that symbolic postconditions must
// remain trust-and-establish-only, never checked, even though the
// sibling conveyor test right above catches the identical claim.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

int
always_negative ()
  post<sc::proven_symbolic_v> (r: r > 0)
{
  return -1;
}

int main () { return always_negative () < 0 ? 0 : 1; }
