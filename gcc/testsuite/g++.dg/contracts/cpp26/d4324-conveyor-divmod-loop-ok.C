// D4324/P2680 item 8, Increment E-divmod: the loop-header merge rule
// (item 4) applied to the "provably nonzero" fact map -- a divisor
// reassigned inside a loop is nonzero-provable after the loop when
// every reassignment is independently nonzero-provable and the
// pre-loop value was already nonzero-provable.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n) conveyor
{
  int b = 3;
  for (int i = 0; i < n; ++i)
    b = 5;
  return 10 / b;
}

int main () { return f (2) - 2; }
