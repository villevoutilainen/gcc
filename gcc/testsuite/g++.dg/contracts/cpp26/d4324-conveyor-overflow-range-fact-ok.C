// D4324/P2680 item 8's overflow scan: the numeric route -- a self-trust
// precondition ('x < 100') gives x an established upper bound well
// below TYPE_MAX, proving 'x++' safe via oa_get_range, with no type-
// bound witness needed at all.
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

int use_range_fact_ok (int x) conveyor pre<conveyor_ctrl_v>(x < 100)
{
  return x++;
}

int main () { return use_range_fact_ok (5) - 5; }
