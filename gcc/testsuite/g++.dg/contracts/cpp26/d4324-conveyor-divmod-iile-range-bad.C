// D4324/P2680 item 8, Increment E4: an immediately-invoked closure
// returning an unrelated, unprovable parameter's value must still be
// correctly rejected via the range-fact IILE return-path merge.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int q) conveyor
{
  int n = [&]() { return q; }();
  return 10 / n; // { dg-error "divisor .n. not provably nonzero in a conveyor function" }
}

int main () { int x = 1; return f (x) - 10; }
