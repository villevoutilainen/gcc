// D4324/P2680: a postcondition's std::is_object_address(r), r being the
// named return-value placeholder, is provable when every return
// statement in the function returns a provable object address (here,
// the single return path returns '&global_x') -- proven, not trusted,
// since the postcondition is checked in this function's own body.
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

int global_x = 42;

int* f () post<conveyor_ctrl_v>(r: std::is_object_address(r))
{
  return &global_x;
}

int main () { return *f () - 42; }
