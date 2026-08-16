// D4324/P2680: companion to d4324-conveyor-field-write-self-check-ok.C --
// the field write's own established value (200.0) is genuinely outside
// the declared postcondition's range ([0,100]), so the field-write
// self-check fix must catch this as a real, provable violation, not
// silently accept it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct thing {
  double m_value;

  thing ()
    post<sc::proven_conveyor_v>(this->m_value >= 0.0 && this->m_value <= 100.0) // { dg-error "postcondition condition .* is provably false" }
  { m_value = 200.0; }
};

int main ()
{
  thing t;
  return 0;
}
