// D4324/P2680: the positive counterpart of d4324-conveyor-pre-
// nonconveyor-function-bad.C -- f's own synthesized is_object_
// address(&x) obligation, now correctly required even without the
// 'conveyor' keyword, is satisfied when the caller passes a genuinely
// provable reference (a named local, whose own address is trivially
// an object address). Uses a trivially-true condition, deliberately:
// this test is about the is_object_address obligation specifically,
// not about x's own value being provably in range (a separate,
// unrelated concern this engine does not track from a plain local
// variable's own initializer).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>

void f (int& x) pre<std::contracts::conveyor_assert_v> (true)
{}

int main ()
{
  int x = 5;
  f (x);
  return 0;
}
