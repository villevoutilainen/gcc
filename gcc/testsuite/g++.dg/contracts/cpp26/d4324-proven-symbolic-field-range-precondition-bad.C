// D4324: companion to d4324-proven-symbolic-field-range-precondition-
// ok.C -- genuine violation. f.m_value is directly, provably negative,
// disjoint from check()'s own required [0.0, +inf) range.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct F
{
  double m_value;

  void check ()
    pre<sc::proven_symbolic_v>(this->m_value >= 0.0)
  { }
};

int
main ()
{
  F f;
  f.m_value = -5.0;
  f.check (); // { dg-error "provably violates the precondition" }
              // { dg-message "is established \[^\n\]*, but the precondition requires" "established fact" { target *-*-* } .-1 }
  return 0;
}
