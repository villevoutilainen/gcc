// D4324: same Number class as d4324-proven-conveyor-number-class-ok.C --
// genuine violation. decrease_by's own precondition requires
// 'percentage <= 100.0'; 150.0 is a literal that provably violates it at
// this call site (the plain literal-vs-literal precondition-argument
// check, independent of everything the -ok.C sibling exercises about the
// interval-multiplication/division/float-named-result corrections).
//
// Note: a smaller, more "natural-looking" out-of-range call, such as
// decrease_by(70.0) right after increase_by(100.0)/decrease_by(20.0),
// does NOT actually violate anything here and must not be used for this
// test -- this->m_value is only ever tracked as a one-sided range (>=
// 0.0), never a tight two-sided one, so the analysis has no way to
// determine that 70.0 would ever push the *real* value negative; only a
// literal that directly contradicts decrease_by's own explicit
// 'percentage <= 100.0' conjunct is something this engine can actually
// prove violated.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct Number
{
  double m_value;

  explicit Number (double value)
    pre<sc::proven_conveyor_v>(value >= 0.0)
    post<sc::proven_conveyor_v>(this->m_value >= 0.0)
  { m_value = value; }

  void increase_by (double percentage)
    pre<sc::proven_conveyor_v>(percentage >= 0.0 && this->m_value >= 0.0)
    post<sc::proven_conveyor_v>(this->m_value >= 0.0)
  { m_value *= (1.0 + percentage / 100.0); }

  void decrease_by (double percentage)
    pre<sc::proven_conveyor_v>(percentage >= 0.0 && percentage <= 100.0
				 && this->m_value >= 0.0)
    post<sc::proven_conveyor_v>(this->m_value >= 0.0)
  { m_value *= (1.0 - percentage / 100.0); }

  double value () const { return m_value; }
};

int
main ()
{
  Number n (50.0);
  n.increase_by (100.0);
  n.decrease_by (150.0); // { dg-error "provably violates the precondition" }
  return 0;
}
