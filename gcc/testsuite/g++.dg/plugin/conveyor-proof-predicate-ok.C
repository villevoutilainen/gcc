// conveyor_proof_plugin.cc: connects produce's postcondition
// ("check_it (r)") to consume's precondition ("check_it (x)") purely
// by name + argument identity -- check_it is a conveyor function whose
// own definition is never seen here, yet the connection still holds
// per the conveyor function rules.  See
// .claude/plans/stateless-jumping-shore.md.
// { dg-do run }
// { dg-additional-sources "conveyor-proof-defs.cc" }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

void caller ()
{
  consume (produce ());
}

int main () { caller (); return 0; }
