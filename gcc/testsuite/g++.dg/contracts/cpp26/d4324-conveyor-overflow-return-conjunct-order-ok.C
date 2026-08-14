// D4324/P2680 item 8's overflow scan: the same left-to-right conjunct
// refinement, for a bare '&&' used directly as a return value (not a
// condition at all) -- '&&'/'||' short-circuit identically regardless of
// why the expression is being evaluated.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

bool f (int x) conveyor
{
  return x < 100000 && x++ < 2048;
}

int main () { return f (1) ? 0 : 1; }
