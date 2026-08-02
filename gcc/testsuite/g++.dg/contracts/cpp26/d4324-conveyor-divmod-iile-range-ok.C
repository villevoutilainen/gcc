// D4324/P2680 item 8, Increment E4: oa_get_range recurses into a
// statically-resolvable, immediately-invoked closure (item 5), merging
// its return paths' range facts by union -- here a single return path
// with a provably-nonzero literal.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f () conveyor
{
  int n = [&]() { return 5; }();
  return 10 / n;
}

int main () { return f () - 2; }
