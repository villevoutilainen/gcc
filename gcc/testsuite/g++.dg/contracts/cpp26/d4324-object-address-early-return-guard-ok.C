// D4324/P2680, Increment H: the reachability-aware merge benefits the
// pre-existing boolean is_object_address-provability map too, not just
// range facts -- the same code path is shared. Here the terminating
// arm (throw) is unrelated to p entirely; p is only ever established
// (as &a) in the non-terminating else-arm, so it should be provable
// after the if. throw can't appear in a conveyor-qualified function
// (stage 1 bans it there), which is why this test uses an ordinary
// function with a conveyor-checked contract_assert instead.
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

int f (bool flag, int a)
{
  int* p;
  if (flag)
    throw 1;
  else
    p = &a;
  contract_assert<conveyor_ctrl_v>(std::is_object_address (p));
  return *p;
}

int main () { return f (false, 5) - 5; }
