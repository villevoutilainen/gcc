// D4324/P2680: companion to d4324-conveyor-assert-object-field-ok.C --
// n.m_value's established range [0,100] is fully disjoint from the
// contract_assert's own required '>= 200.0', a genuine, provable
// violation.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct Number
{
  double m_value;
  explicit Number (double value)
    pre<sc::proven_conveyor_v>(value >= 0.0 && value <= 100.0)
    post<sc::proven_conveyor_v>(this->m_value >= 0.0 && this->m_value <= 100.0)
  { m_value = value; }
};

int main ()
{
  Number n (50.0);
  contract_assert<sc::proven_conveyor_v>(n.m_value >= 200.0); // { dg-error "is provably false" }
                                                                // { dg-message "established \[^\n\]*" "established fact" { target *-*-* } .-1 }
  return 0;
}
