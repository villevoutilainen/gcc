// conveyor_proof_plugin.cc: produce_bad's postcondition guarantees
// "!check_it (r)" -- the exact opposite polarity of what consume's
// precondition requires for that same value.  A genuine, provable
// contradiction, still without ever evaluating check_it itself.  See
// .claude/plans/stateless-jumping-shore.md.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

void caller3 ()
{
  consume (produce_bad ()); // { dg-error "provably violates the precondition" }
}

int main () { caller3 (); return 0; }
