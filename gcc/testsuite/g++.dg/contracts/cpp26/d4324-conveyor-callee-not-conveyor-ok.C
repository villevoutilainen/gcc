// D4324: same shape as d4324-conveyor-callee-not-conveyor-bad.C, but
// the callee is also declared 'conveyor' -- must compile and run
// cleanly. See .claude/plans/well-we-last-discussed-ethereal-
// duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int helper (int x) conveyor { return x; }

int f (int x) conveyor
{
  return helper (x);
}

int main () { return f (1) - 1; }
