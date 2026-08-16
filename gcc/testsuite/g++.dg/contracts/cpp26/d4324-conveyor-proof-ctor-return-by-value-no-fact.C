// D4324/P2680: documents a deliberate, disclosed limitation of the
// ctor-return-by-value/by-value-argument/array-init fix (see
// d4324-conveyor-proof-ctor-return-by-value-ok.C and siblings): no
// postcondition-based fact gets established for the caller in any of
// these shapes, because AGGR_INIT_EXPR_SLOT (the real receiver) is
// always a fresh, anonymous compiler temporary at this pre-genericize
// stage -- never a stable, resolvable identity (that mapping is only
// established later, during gimplification). A contract_assert
// immediately consulting the constructed object's own field correctly
// says "cannot prove", never a false "proven true" -- confirmed here,
// not just assumed.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct Number {
  double m_value;
  explicit Number (double value)
    pre<sc::proven_conveyor_v>(value >= 0.0 && value <= 100.0)
    post<sc::proven_conveyor_v>(this->m_value >= 0.0 && this->m_value <= 100.0)
  { m_value = value; }
};

Number make () { return Number (50.0); }

int main ()
{
  Number n = make ();
  contract_assert<sc::proven_conveyor_v>(n.m_value >= 0.0); // { dg-error "cannot prove" }
  return 0;
}
