// gimple_object_address_plugin.cc: item 6's own shape for ranges --
// make_val()'s declared postcondition unconditionally guarantees
// "r >= 20 && r < 100" for its own return value, read declaratively off
// make_val()'s own FUNCTION_DECL (call_postcondition_range_p).
// established_range_of recognizes 'y's own def-stmt as a plain copy of
// a GIMPLE_CALL's own return temporary (the shape gimplification
// actually produces for 'int y = make_val();' -- found empirically,
// see ~/gimple-contract-analysis.md), so consumer(y)'s own obligation
// is discharged purely from that declared guarantee.
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

int make_val () post<conveyor_ctrl_v>(r: r >= 20 && r < 100) { return 50; }

void consumer (int x) pre<conveyor_ctrl_v>(x >= 20 && x < 100) { (void) x; }

void h () { int y = make_val (); consumer (y); }

int main () { h (); return 0; }
