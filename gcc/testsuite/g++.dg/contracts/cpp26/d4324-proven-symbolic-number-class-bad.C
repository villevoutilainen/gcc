// D4324: same Number class as d4324-proven-symbolic-number-class-ok.C
// -- genuine violation. decrease_by's own precondition requires
// 'percentage <= 100.0'; 180.0 provably violates it (the literal
// combined-range precondition mechanism -- see d4324-proven-symbolic-
// precondition-literal-range-bad.C -- catching exactly the shape the
// original compile_assert-based file used compile_assert to merely
// *describe*, never actually enforce, since compile_assert's own checks
// are no-ops outside a constant-evaluated context).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct Number
{
  double m_value;

  explicit Number (double value)
    pre<sc::proven_symbolic_v>(value >= 0.0)
    post<sc::proven_symbolic_v>(this->m_value >= 0.0)
  { m_value = value; }

  void increase_by (double percentage)
    pre<sc::proven_symbolic_v>(percentage >= 0.0 && percentage <= 100.0)
    post<sc::proven_symbolic_v>(this->m_value >= 0.0)
  { m_value *= (1.0 + percentage / 100.0); }

  void decrease_by (double percentage)
    pre<sc::proven_symbolic_v>(percentage >= 0.0 && percentage <= 100.0)
    post<sc::proven_symbolic_v>(this->m_value >= 0.0)
  { m_value *= (1.0 - percentage / 100.0); }

  double value () const { return m_value; }
};

int
main ()
{
  Number n (50.0);
  n.increase_by (100.0);
  n.decrease_by (20.0);
  n.decrease_by (180.0); // { dg-error "provably violates the precondition" }
  return 0;
}
