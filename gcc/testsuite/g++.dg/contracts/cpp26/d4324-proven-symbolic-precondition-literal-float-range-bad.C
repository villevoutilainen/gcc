// D4324: the floating-point analogue of d4324-proven-symbolic-
// precondition-literal-range-bad.C -- same shared function
// (oa_handle_precondition_simple_range_obligation) handles both int and
// float uniformly, so this is the same fix confirmed for float under
// proven_symbolic too.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

void
take_percentage (double p) pre<sc::proven_symbolic_v>(p >= 0.0 && p <= 100.0)
{
  (void) p;
}

void
bad_call ()
{
  take_percentage (150.0); // { dg-error "provably violates the precondition" }
}

int main () { return 0; }
