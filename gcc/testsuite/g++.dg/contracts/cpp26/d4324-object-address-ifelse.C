// D4324/P2680: the if/else merge rule -- std::is_object_address(p) is
// provable after a branch where both arms independently assign p a
// provable value ('&a' / '&b' here), exactly the "every incoming def
// must satisfy it" PHI-style rule, applied over the (still-structured,
// pre-genericize) IF_STMT tree rather than a flattened CFG join point.
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

int f (int x)
{
  int a = 1, b = 2;
  int* p;
  if (x > 0)
    p = &a;
  else
    p = &b;
  contract_assert<conveyor_ctrl_v>(std::is_object_address(p));
  return *p;
}

int main () { return f (1) - 1; }
