// Companion to conveyor-proof-postcondition-ok.C: a postcondition its own
// function's body provably breaks (the only return gives -1) is an error.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

int negative_one () post<conveyor_ctrl_v> (r: r > 0) // { dg-error "postcondition condition .* is provably false" }
{
  return -1;
}

int main () { return negative_one () + 1; }
