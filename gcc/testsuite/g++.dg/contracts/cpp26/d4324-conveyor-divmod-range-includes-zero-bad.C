// D4324/P2680 item 8, Increment E1: a guard that doesn't actually
// exclude zero ('n >= 0' still permits n == 0) must not be accepted as
// establishing nonzero-ness, confirming the refinement is a real
// interval computation, not a loose heuristic.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n) conveyor
{
  if (n >= 0)
    return 10 / n; // { dg-error "divisor .n. not provably nonzero in a conveyor function" }
  return 0;
}

int main () { int x = 1; return f (x) - 2; }
