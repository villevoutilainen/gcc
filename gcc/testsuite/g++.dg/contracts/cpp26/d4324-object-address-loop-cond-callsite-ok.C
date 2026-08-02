// D4324/P2680, Increment N: item 7's call-site precondition-obligation
// scan now applies to a loop's own condition too, matching IF_STMT/
// COND_EXPR/SWITCH_STMT's conditions.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct conveyor_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr conveyor_ctrl conveyor_ctrl_v{};

bool check (int* p) pre<conveyor_ctrl_v>(std::is_object_address (p))
{
  return *p > 0;
}

int f ()
{
  int x = 1;
  int total = 0;
  for (int i = 0; i < 3 && check (&x); i++)
    total += i;
  return total;
}

int main () { return f () == 3 ? 0 : 1; }
