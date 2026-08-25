// D4324: never_proven, symbolic flavor -- same exemption as
// d4324-never-proven-conveyor-ok.C, using the built-in
// std::contracts::never_proven_symbolic_v (is_symbolic + never_proven).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs" }

#include <contracts>
namespace sc = std::contracts;

bool is_opened (int*) symbolic;

int
f (int* p)
{
  // is_opened's own truth is never established anywhere -- an ordinary
  // analyzed_symbolic/proven_symbolic contract would warn or error;
  // never_proven produces no diagnostic at all.
  contract_assert<sc::never_proven_symbolic_v>(is_opened (p));
  return 0;
}

int main () { int x = 0; return f (&x); }
