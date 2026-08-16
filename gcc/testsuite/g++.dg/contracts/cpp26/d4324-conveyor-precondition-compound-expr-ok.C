// D4324/P2680: -fcontract-conveyor-proofs, a precondition conjunct whose
// non-literal side is a *compound* expression ('this->m_value + delta'),
// not a bare field or parameter, is genuinely checked against the
// literal, not silently ignored (previously matched by no conjunct-shape
// recognizer in the file, with zero diagnostic either way).  set_value()'s
// postcondition establishes this->m_value in [0,100]; bump()'s precondition
// requires this->m_value + delta in [0,200] -- [50,150] (delta literal 50)
// is a subset, so the obligation is discharged silently.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct thing {
  double m_value;

  void set_value ()
    post<sc::proven_conveyor_v>(this->m_value >= 0.0 && this->m_value <= 100.0)
  { m_value = 50.0; }

  void bump (double delta)
    pre<sc::proven_conveyor_v>(m_value + delta >= 0.0 && m_value + delta <= 200.0)
  { }
};

int main ()
{
  thing t;
  t.set_value ();
  t.bump (50.0);
  return 0;
}
