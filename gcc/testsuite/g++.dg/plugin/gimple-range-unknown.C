// gimple_object_address_plugin.cc: general range checking's own
// OA_UNKNOWN case -- 'm' is a plain, unconstrained integer parameter
// with no established fact of any kind and no derivable range from
// ordinary code either (the ranger itself reports it as varying), so
// there is nothing for consumer's own precondition obligation to
// consult. As with nonzero-ness (unlike is_object_address), there is
// no mandatory call-site obligation check for general ranges either,
// so this compiles and runs successfully with only this prototype's
// own warning. See ~/gimple-contract-analysis.md.
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

void consumer (int x) pre<conveyor_ctrl_v>(x >= 20 && x < 100) { (void) x; }

void relay (int m)
{
  consumer (m); // { dg-warning "gimple-oa: cannot verify" }
}

int main () { relay (50); return 0; }
