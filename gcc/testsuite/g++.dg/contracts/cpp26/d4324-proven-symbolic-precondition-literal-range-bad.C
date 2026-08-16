// D4324: same mechanism as d4324-proven-symbolic-precondition-literal-
// range-ok.C, genuine violation -- an out-of-range literal argument
// must now be caught under proven_symbolic (previously silently
// unchecked for this exact shape).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

void
take_percentage (int p) pre<sc::proven_symbolic_v>(p >= 0 && p <= 100)
{
  (void) p;
}

void
bad_call ()
{
  take_percentage (150); // { dg-error "provably violates the precondition" }
}

int main () { return 0; }
