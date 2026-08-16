// D4324: the call-relational shape ("decl OP receiver.accessor ()") is
// also checked -- f's own precondition establishes 'x < v.size ()' as
// a fact; the contract_assert's own claim 'x > v.size ()' flatly
// contradicts it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct S {
  int size () const conveyor { return 5; }
};

int
f (int x, S& v) conveyor pre<sc::conveyor_assert_v>(x < v.size ())
{
  contract_assert<sc::conveyor_assert_v>(x > v.size ()); // { dg-error "condition .*size.*is provably false" }
  return 0;
}

int
main ()
{
  S v;
  return f (1, v); // { dg-warning "cannot verify that .1. satisfies the precondition" }
}
