// D4324/P2680 item 8's overflow scan: the companion to ...-bad.C -- the
// true branch is scanned using the condition's own refinement (via
// oa_process_condition's then_env), so 'a != 0' correctly proves the
// division inside that branch safe.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int a) conveyor
{
  return (a != 0) ? 10 / a : 0;
}

int main () { return f (5) - 2; }
