// conveyor_proof_plugin.cc: connects produce's postcondition
// ("check_it (r)") to consume's precondition ("check_it (x)") purely
// by name + argument identity -- check_it is a conveyor function whose
// own definition is never seen here, yet the connection still holds
// per the conveyor function rules.  Now via the real, cross-statement-
// tracked fact-tracking engine (m_predicate_fact_map) rather than a
// purely syntactic single-hop check, so the value must be named by an
// intermediate variable first (exactly like the numeric-comparison
// scenarios' own r = compute_positive() pattern) for oa_object_
// identity_decl to have a stable key to track it by.  See
// .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do run }
// { dg-additional-sources "conveyor-proof-defs.cc" }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

void caller ()
{
  int r = produce ();
  consume (r);
}

int main () { caller (); return 0; }
