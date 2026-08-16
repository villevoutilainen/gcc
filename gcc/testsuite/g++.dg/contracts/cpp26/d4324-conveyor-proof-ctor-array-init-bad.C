// D4324/P2680: companion to d4324-conveyor-proof-ctor-array-init-ok.C --
// the first element's own argument, -5.0, is disjoint from the
// constructor's required [0,100], a genuine, provable violation, caught
// via the new INDIRECT_REF recursion in oa_walk_stmt reaching the
// element's own AGGR_INIT_EXPR at all.
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

int main ()
{
  Number arr[2] = { Number (-5.0), Number (2.0) }; // { dg-error "provably violates the precondition" }
  return 0;
}
