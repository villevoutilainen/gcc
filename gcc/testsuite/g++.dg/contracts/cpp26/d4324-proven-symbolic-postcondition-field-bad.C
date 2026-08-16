// D4324: direct regression test for the design correction itself (found
// via the Number godbolt demo) -- a symbolic postcondition's self-check
// is no longer blanket-exempt. increase()'s body can provably push
// m_value outside the range its own post<proven_symbolic_v>(...) claims
// (percentage isn't bounded above, so m_value += percentage has no
// provable upper bound), which previously compiled clean solely because
// the postcondition was symbolic; now correctly caught, exactly as the
// identical shape already is under proven_conveyor.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct Number {
  double m_value;
  explicit Number (double value)
    pre<sc::proven_symbolic_v>(value >= 0.0 && value <= 100.0)
    post<sc::proven_symbolic_v>(this->m_value >= 0.0 && this->m_value <= 100.0)
  { m_value = value; }

  void increase (double percentage)
    pre<sc::proven_symbolic_v>(percentage >= 0.0)
    post<sc::proven_symbolic_v>(this->m_value >= 0.0 // { dg-error "cannot prove postcondition condition" }
				 && this->m_value <= 100.0) // { dg-error "cannot prove postcondition condition" }
  { m_value += percentage; }
};

int main ()
{
  Number n (50.0);
  n.increase (1000.0);
  return 0;
}
