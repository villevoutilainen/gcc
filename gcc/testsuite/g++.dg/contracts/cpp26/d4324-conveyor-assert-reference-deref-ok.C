// D4324/P2680: the positive counterpart of d4324-conveyor-assert-
// reference-deref-bad.C -- a reference parameter's is_object_address
// obligation, inside a body contract_assert, is satisfied once self-
// trust already covers it (here, via f's own 'conveyor' keyword and
// its own synthesized precondition) -- and a purely local reference/
// capture binding (never a parameter) remains exempt exactly as
// before, since its own binding would already have been checked at
// its own point of initialization by this same mandatory pass.
// never_proven_conveyor_v, deliberately: this test is about the
// is_object_address obligation specifically, not about x/r's own
// value being provably in range (a separate, unrelated concern this
// engine does not track from a plain parameter/local's own value).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>

void f (int& x) conveyor
{
  contract_assert<std::contracts::never_proven_conveyor_v> (x < 1000000);
}

void g (int y)
{
  int& r = y;
  contract_assert<std::contracts::never_proven_conveyor_v> (r < 1000000);
}

int main ()
{
  int x = 5;
  f (x);
  g (3);
  return 0;
}
