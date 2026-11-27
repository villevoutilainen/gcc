// conveyor_proof_plugin.cc checks a callee's precondition comparing a
// floating-point parameter with a float literal, as the built-in
// -fcontract-conveyor-proofs does: a literal argument, and a value
// refined by a dominating condition, are proven within the bounds.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

void set_percentage (double v) pre<conveyor_ctrl_v> (v >= 0.0 && v <= 100.0);

struct Number
{
  double m_value;
  explicit Number (double value) pre<conveyor_ctrl_v> (value >= 0.0 && value <= 100.0)
  { m_value = value; }
};

void literal () { set_percentage (50.0); }
void refined (double d) { if (d >= 1.0 && d < 2.0) set_percentage (d); }
void ctor () { Number n (25.5); (void) n; }

int main () { literal (); refined (1.5); ctor (); return 0; }
