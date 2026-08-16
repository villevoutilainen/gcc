// D4324/P2680: the by-value-argument sibling of d4324-conveyor-proof-
// ctor-return-by-value-ok.C -- 'take(Number(50.0))' constructs its
// argument via the same AGGR_INIT_EXPR shape. 50.0 satisfies the
// precondition, silently, correctly discharged.
// { dg-do run { target c++26 } }
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

int main () { take (Number (50.0)); return 0; }
