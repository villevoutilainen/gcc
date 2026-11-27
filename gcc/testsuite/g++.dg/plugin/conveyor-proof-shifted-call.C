// conveyor_proof_plugin.cc checks a shifted comparison against a call
// ('idx - v.size () < 0') as the built-in -fcontract-conveyor-proofs does:
// a caller's own precondition establishing the opposite is a violation.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

// Bounded, so that 'idx - v.size ()' is provably free of overflow.
struct S
{
  int size () const conveyor post<conveyor_ctrl_v>(r: r >= 0 && r <= 5)
  { return 5; }
};

int consumer (const S& v, int idx) conveyor
  pre<conveyor_ctrl_v>(idx >= 0 && idx <= 1000 && idx - v.size () < 0)
{ return idx; }

int producer (const S& v, int idx) conveyor
  pre<conveyor_ctrl_v>(idx >= 0 && idx <= 1000 && idx - v.size () > 10)
{
  return consumer (v, idx); // { dg-error "provably violates the precondition" }
}

int main () { return 0; }
