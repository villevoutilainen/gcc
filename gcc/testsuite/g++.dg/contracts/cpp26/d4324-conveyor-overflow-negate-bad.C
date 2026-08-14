// D4324/P2680 item 8's overflow scan: NEGATE_EXPR ('-x') is checked --
// an unconstrained parameter's own negation could hit TYPE_MIN, and
// nothing here establishes otherwise.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int x) conveyor
{
  return -x; // { dg-error "negation of .x. not provably free of overflow" }
}

int main () { return f (1); }
