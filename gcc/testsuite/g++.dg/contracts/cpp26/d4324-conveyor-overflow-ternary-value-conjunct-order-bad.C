// D4324/P2680 item 8's overflow scan: the mirror-image conjunct order in
// a ternary's own condition, still correctly rejected.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int x) conveyor
{
  int y = (x++ < 2048 && x < 100000) ? 1 : 2; // { dg-error "increment of .x. not provably free of overflow" }
  return y;
}

int main () { return f (1); }
