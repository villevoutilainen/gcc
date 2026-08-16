// D4324: proven_symbolic, symbolic mirror of
// d4324-proven-conveyor-unknown-bad.C -- strict, forces analysis on
// with no -fcontract-symbolic-proofs anywhere in dg-additional-
// options, and an unprovable conjunct is a hard error, not a warning.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

bool is_opened (int*) symbolic;

int
f (int* p)
{
  contract_assert<sc::proven_symbolic_v>(is_opened (p)); // { dg-error "cannot prove" }
  return 0;
}

int main () { int x = 0; return f (&x); }
