// D4324/P2680 item 8, Increment H: an early-return guard that doesn't
// actually exclude zero ('n < 0' still permits n == 0) must not be
// accepted, confirming the reachability-aware merge doesn't just
// blindly trust the surviving branch -- it still only survives with
// whatever range oa_refine_single_comparison actually established.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n) conveyor
{
  if (n < 0)
    return 0;
  return 10 / n; // { dg-error "divisor .n. not provably nonzero in a conveyor function" }
}

int main () { int x = 1; return f (x) - 2; }
