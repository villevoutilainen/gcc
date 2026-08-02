// D4324/P2680: recursing into a statically-resolvable, immediately-
// invoked closure (item 5) -- the IILE's own by-reference capture of
// 'a' is resolved back against the enclosing scope, and the closure's
// return value is provable on its only return path, so the whole
// expression is provable.
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

int g ()
{
  int a = 1;
  int* p = [&]() { return &a; }();
  contract_assert<conveyor_ctrl_v>(std::is_object_address(p));
  return *p;
}

int main () { return g () - 1; }
