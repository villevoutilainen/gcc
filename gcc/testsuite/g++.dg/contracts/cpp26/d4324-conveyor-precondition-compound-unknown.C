// D4324: a precondition comparing arithmetic of the callee's parameters
// or fields is checked at the call like any other conjunct -- proven,
// a provable violation, or reported as not verified; never passed over.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>

void f (int a, int b)
  pre<std::contracts::proven_conveyor_v>(a >= 0 && a < 100 && b >= 0 && b < 100 && a - b >= 0)
{}

int main (int argc, char**)
{
  if (argc >= 0 && argc < 50)
    {
      f (argc, 3);	// { dg-error "cannot prove that .\\(argc - 3\\). satisfies the precondition" }
      f (60, argc);
      f (2, 50);	// { dg-error "provably violates the precondition" }
    }
  return 0;
}
