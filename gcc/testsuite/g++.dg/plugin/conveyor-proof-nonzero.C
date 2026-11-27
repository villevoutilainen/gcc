// conveyor_proof_plugin.cc checks a callee's 'x != 0' precondition, as the
// built-in -fcontract-conveyor-proofs does: a nonzero literal or a value
// refined nonzero is fine, a zero literal provably violates it, and a
// value the caller knows nothing about can't be verified.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

int divide (int a, int d) pre<conveyor_ctrl_v> (d != 0);

int literal (int a) { return divide (a, 2); }
int refined (int a, int d) { return d != 0 ? divide (a, d) : 0; }
int zero (int a) { return divide (a, 0); } // { dg-error "provably violates the precondition" }
int unknown (int a, int d) { return divide (a, d); } // { dg-warning "cannot verify that .d. is nonzero" }

int main () { return literal (4) + refined (4, 2) + zero (1) + unknown (4, 2); }
