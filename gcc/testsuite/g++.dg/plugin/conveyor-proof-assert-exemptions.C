// conveyor_proof_plugin.cc treats a contract_assert as the built-in
// -fcontract-conveyor-proofs does: a never_proven one is trusted as
// written, a bare std::is_object_address conjunct is left to the
// mandatory check, and an unprovable conjunct is an error for a strict
// (proven_conveyor) control object.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

void trusted (int& x) conveyor
{
  contract_assert<sc::never_proven_conveyor_v> (x < 1000000);
}

void address (int* p)
{
  contract_assert<sc::never_proven_conveyor_v> (std::is_object_address (p));
  contract_assert<conveyor_ctrl_v> (std::is_object_address (p));
}

void strict (int x)
{
  contract_assert<sc::proven_conveyor_v> (x > 0); // { dg-error "cannot prove 'contract_assert' condition" }
}

int main () { int i = 1; trusted (i); address (&i); strict (1); return 0; }
