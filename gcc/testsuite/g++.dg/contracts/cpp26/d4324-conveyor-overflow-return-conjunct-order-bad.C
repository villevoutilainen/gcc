// D4324/P2680 item 8's overflow scan: the mirror-image conjunct order in
// a return value, still correctly rejected.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

bool f (int x) conveyor
{
  return x++ < 2048 && x < 100000; // { dg-error "increment of .x. not provably free of overflow" }
}

int main () { return f (1) ? 0 : 1; }
