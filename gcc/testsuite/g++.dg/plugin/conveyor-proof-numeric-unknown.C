// conveyor_proof_plugin.cc: OA_UNKNOWN for a plain comparison conjunct
// -- an ordinary, uncontracted parameter with no established range
// fact at all.  A weaker signal than a proven violation: a warning,
// not an error, and compilation still succeeds -- see
// .claude/plans/stateless-jumping-shore.md.
// { dg-do run }
// { dg-additional-sources "conveyor-proof-defs.cc" }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

void caller3 (int untrusted)
{
  use_positive (untrusted); // { dg-warning "cannot verify" }
}

int main () { caller3 (1); return 0; }
