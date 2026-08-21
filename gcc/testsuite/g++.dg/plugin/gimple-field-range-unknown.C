// gimple_object_address_plugin.cc: field-range's own OA_UNKNOWN case --
// 'p' is a plain, unconstrained pointer parameter with no established
// field fact of any kind, so there is nothing for consume()'s own
// precondition obligation to consult. No mandatory call-site obligation
// check exists for field-range facts either (same as named predicates/
// nonzero/general ranges), so this compiles and runs successfully with
// only this prototype's own warning. See ~/gimple-contract-analysis.md.
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

struct thing {
  int count;
  void consume ()
    pre<conveyor_ctrl_v>(std::is_object_address (this))
    pre<conveyor_ctrl_v>(this->count >= 20 && this->count < 1000)
  { }
};

void relay (thing *p)
  pre<conveyor_ctrl_v>(std::is_object_address (p))
{
  p->consume (); // { dg-warning "gimple-oa: cannot verify" }
}

int main () { thing t; t.count = 50; relay (&t); return 0; }
