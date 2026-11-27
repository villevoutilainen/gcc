// Companion to conveyor-proof-postcondition-ok.C: a postcondition the body
// gives no grounds for (it returns its argument unchecked) can't be
// verified -- a warning; for a never_proven postcondition, nothing (it is
// trusted as written).
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

int pass_through (int x) post<conveyor_ctrl_v> (r: r > 0) // { dg-warning "cannot verify postcondition condition" }
{
  return x;
}

int trusted (int x) post<sc::never_proven_conveyor_v> (r: r > 0)
{
  return x;
}

int main () { return pass_through (1) + trusted (1) - 2; }
