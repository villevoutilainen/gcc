// conveyor_proof_plugin.cc: produce_bad's postcondition guarantees
// "!check_it (r)" -- the exact opposite polarity of what consume's
// precondition requires for that same value.  A genuine, provable
// contradiction, still without ever evaluating check_it itself.  Via
// the real fact-tracking engine, same intermediate-variable requirement
// as conveyor-proof-predicate-ok.C -- see .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

void caller3 ()
{
  int r = produce_bad ();
  consume (r); // { dg-error "provably violates the precondition" }
}

int main () { caller3 (); return 0; }
