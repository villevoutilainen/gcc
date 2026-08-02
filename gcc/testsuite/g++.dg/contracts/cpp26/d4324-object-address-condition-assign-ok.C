// D4324/P2680: closes the "assignment-in-condition" gap for the
// is_object_address-provability fact too -- 'if ((p = &x) != nullptr)'
// now updates p's tracked provability (via oa_track_condition_assignment),
// so a later contract_assert<...>(std::is_object_address(p)) inside the
// then-branch succeeds.
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
  int* p = nullptr;
  if ((p = &x) != nullptr)
    contract_assert<conveyor_ctrl_v>(std::is_object_address(p));
  return *p;
}

int main () { return f (1) - 1; }
