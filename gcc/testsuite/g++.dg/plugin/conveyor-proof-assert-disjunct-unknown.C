// Companion to conveyor-proof-assert-disjunct-ok.C: a disjunctive
// contract_assert condition ('x > 0 || x < -1000') where NEITHER
// disjunct is independently provable (x is unconstrained) and neither
// is independently provable false either -- OA_UNKNOWN, not a
// contradiction. See .claude/plans/lazy-stirring-pearl.md, Tier 3b.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

void disjunct_unknown (int x)
{
  contract_assert<conveyor_ctrl_v> (x > 0 || x < -1000); // { dg-warning "cannot verify" }
}

int main () { disjunct_unknown (5); return 0; }
