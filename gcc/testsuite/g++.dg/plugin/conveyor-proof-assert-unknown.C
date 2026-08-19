// Companion to conveyor-proof-assert-ok.C: OA_UNKNOWN for a plain
// contract_assert statement's own condition -- x is an ordinary,
// unconstrained parameter with no established range fact at all. See
// .claude/plans/lazy-stirring-pearl.md, Tier 3b.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

void unknown_case (int x)
{
  contract_assert<conveyor_ctrl_v> (x > 0); // { dg-warning "cannot verify" }
}

int main () { unknown_case (1); return 0; }
