// Axiom contracts ("the gem", Mechanism B): -fcontract-symbolic-
// runtime-checks, the not-subsumed case -- producer()'s postcondition
// establishes [40,100) for its return value, but consumer()'s
// precondition requires [200,1000), which [40,100) is not a subset of;
// the control object's operator() must see ctx.check() fail.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks" }

#include <contracts>
namespace sc = std::contracts;

bool checked = false;
bool failed = false;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  {
    checked = true;
    if (!ctx.check ())
      failed = true;
  }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

int producer () post<symbolic_ctrl_v>(r: r >= 40 && r < 100) { return 55; }
void consumer (int x) pre<symbolic_ctrl_v>(x >= 200 && x < 1000) { (void) x; }

int main ()
{
  int y = producer ();
  consumer (y);
  if (!checked || !failed)
    __builtin_abort ();
  return 0;
}
