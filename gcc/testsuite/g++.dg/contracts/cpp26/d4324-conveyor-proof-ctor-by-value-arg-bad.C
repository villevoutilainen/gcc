// D4324/P2680: companion to d4324-conveyor-proof-ctor-by-value-arg-
// ok.C -- -5.0 is disjoint from the constructor's required [0,100], a
// genuine, provable violation.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct Number {
  double m_value;
  explicit Number (double value)
    pre<sc::proven_conveyor_v>(value >= 0.0 && value <= 100.0)
  { m_value = value; }
};

void take (Number n) { }

int main () { take (Number (-5.0)); return 0; } // { dg-error "provably violates the precondition" }
