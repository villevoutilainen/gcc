// A plugin's walk (oa_walk_function_calls, from PLUGIN_PRE_GENERICIZE)
// sees the body the built-in engine's walk does: non-ODR uses of
// constants folded, so a never_proven trust through a reference to a
// global ('H &r = g; ... &r', folded to '&g') makes the global an
// object again after a call that may have ended it.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

struct H { int v; void cm () const conveyor {} };
void other ();
H g;

void trusted ()
{
  other ();
  H& r = g;
  contract_assert<sc::never_proven_conveyor_v> (std::is_object_address (&r));
  g.cm ();
}

void untrusted () { g.cm (); other (); g.cm (); } // { dg-error "cannot prove .is_object_address." }

int main () { return 0; }
