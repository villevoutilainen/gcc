// D4324/P2680 item 8: an assignment-in-condition guard that doesn't
// actually exclude zero ('(i = compute (q)) >= 0' still permits
// i == 0) must not be accepted as establishing nonzero-ness, the same
// "real interval computation, not a loose heuristic" discipline
// d4324-conveyor-divmod-range-includes-zero-bad.C already checks for
// a plain (non-assignment) condition.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int compute (int) conveyor;

int f (int q) conveyor
{
  int i = 0;
  if ((i = compute (q)) >= 0)
    return 10 / i; // { dg-error "divisor .i. not provably nonzero in a conveyor function" }
  return 0;
}

int main () { int x = 1; return f (x) - 2; }
