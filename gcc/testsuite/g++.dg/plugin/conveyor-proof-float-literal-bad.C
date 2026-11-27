// Companion to conveyor-proof-float-literal-ok.C: a literal argument
// outside the float bounds provably violates the precondition, and a
// value the caller knows nothing about can't be verified.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

void set_percentage (double v) pre<conveyor_ctrl_v> (v >= 0.0 && v <= 100.0);

void too_big () { set_percentage (150.0); } // { dg-error "provably violates the precondition" }
void unknown (double d) { set_percentage (d); } // { dg-warning "cannot verify" }

int main () { too_big (); unknown (1.0); return 0; }
