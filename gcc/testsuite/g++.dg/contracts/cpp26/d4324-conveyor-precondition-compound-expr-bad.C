// D4324/P2680: companion to d4324-conveyor-precondition-compound-expr-
// ok.C -- this->m_value in [0,100], delta literal 1000, so this->m_value
// - delta is in [-1000,-900], wholly disjoint from bump_bad's own
// required '>= 0.0', a genuine, provable violation.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct thing {
  double m_value;

  void set_value ()
    pre<sc::proven_conveyor_v>(std::is_object_address (this))
    post<sc::proven_conveyor_v>(this->m_value >= 0.0 && this->m_value <= 100.0)
  { m_value = 50.0; }

  void bump_bad (double delta)
    pre<sc::proven_conveyor_v>(std::is_object_address (this))
    pre<sc::proven_conveyor_v>(m_value - delta >= 0.0)
  { }
};

int main ()
{
  thing t;
  t.set_value ();
  t.bump_bad (1000.0); // { dg-error "provably violates the precondition" }
  return 0;
}
