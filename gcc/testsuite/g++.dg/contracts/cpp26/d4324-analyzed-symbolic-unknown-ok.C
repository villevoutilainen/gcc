// D4324: analyzed_symbolic, symbolic mirror of
// d4324-analyzed-conveyor-unknown-ok.C -- forces -fcontract-symbolic-
// proofs-equivalent analysis on with no such flag anywhere in dg-
// additional-options, lenient (unknown warns, not errors).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

bool is_opened (int*) symbolic;

int
f (int* p)
{
  contract_assert<sc::analyzed_symbolic_v>(is_opened (p)); // { dg-warning "cannot verify" }
  return 0;
}

int main () { int x = 0; return f (&x); }
