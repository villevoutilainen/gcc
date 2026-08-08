// gimple_object_address_plugin.cc: nonzero-ness's own OA_UNKNOWN case
// -- 'm' is a plain, unconstrained integer parameter with no
// established fact of any kind, so there is nothing for consumer's own
// precondition obligation to consult. Unlike is_object_address (whose
// own mandatory item-7 check hard-errors on a genuinely unprovable
// case, blocking GIMPLE from ever being generated for the whole
// translation unit -- see ~/gimple-contract-analysis.md, Section 8.2),
// there is no mandatory call-site obligation check for nonzero-ness at
// all, so this compiles and runs successfully with only this
// prototype's own warning. The sound answer is "cannot verify," not
// silent acceptance.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

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

int consumer (int n) pre<conveyor_ctrl_v>(n != 0) { return 10 / n; }

int relay (int m)
{
  return consumer (m); // { dg-warning "gimple-oa: cannot verify" }
}

int main () { return relay (5) - 2; }
