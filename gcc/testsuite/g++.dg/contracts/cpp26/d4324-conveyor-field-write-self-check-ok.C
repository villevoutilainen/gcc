// D4324/P2680: -fcontract-conveyor-proofs, a direct field write ('m_value
// = value;') establishes a fresh field-range fact usable by the SAME
// function's own postcondition self-check -- a distinct mechanism from
// establishing a fact for a *caller* after the call returns (already
// covered by d4324-conveyor-proof-field-range-ok.C).  Before this fix,
// even a trivially-true postcondition here failed to self-check, since
// the field-write "Stage" block only ever invalidated, never
// established, a fact for its own function's own body to use.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct thing {
  double m_value;

  explicit thing (double value)
    pre<sc::proven_conveyor_v>(value >= 0.0 && value <= 100.0)
    post<sc::proven_conveyor_v>(this->m_value >= 0.0 && this->m_value <= 100.0)
  { m_value = value; }
};

int main ()
{
  thing t (50.0);
  return 0;
}
