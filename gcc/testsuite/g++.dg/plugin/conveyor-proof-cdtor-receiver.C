// conveyor_proof_plugin.cc checks a constructor's or destructor's
// receiver at a plain constructor or destructor call, as the built-in
// -fcontract-conveyor-proofs does: a local object's destructor at the
// end of its scope is fine; an explicit destructor call through an
// unproven pointer can't be verified.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

struct H { int* p; H (int* q) : p (q) {} ~H () {} };

void kill (H* p)
{
  p->~H (); // { dg-warning "cannot verify .is_object_address. for .p., required by the precondition of .H::~H\\(\\)." }
}

void local () { int i = 0; H h (&i); (void) h; }

int main () { local (); return 0; }
