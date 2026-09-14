// Companion to d4324-symbolic-runtime-scalar-literal-ok.C -- a literal
// argument that genuinely violates the precondition (-1, outside
// [20,1000)) must be dispatched and genuinely FAIL, confirming the fix
// there checks the literal's own real value rather than unconditionally
// accepting (or, before the fix, silently never checking at all) any
// literal argument.
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

void consumer (int x) pre<symbolic_ctrl_v>(x >= 20 && x < 1000) { (void) x; }

int main ()
{
  consumer (-1);
  if (!checked || !failed)
    __builtin_abort ();
  return 0;
}
