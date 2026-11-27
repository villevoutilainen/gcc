// conveyor_proof_plugin.cc checks a function's own postcondition against
// its body, at its exit (merged across every return), as the built-in
// -fcontract-conveyor-proofs does: each of these holds on every return.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

int clamp (int x) post<conveyor_ctrl_v> (r: r >= 0 && r <= 100)
{
  if (x < 0)
    return 0;
  if (x > 100)
    return 100;
  return x;
}

int positive () post<conveyor_ctrl_v> (r: r > 0)
{
  return 42;
}

int main () { return clamp (5) + positive () - 47; }
