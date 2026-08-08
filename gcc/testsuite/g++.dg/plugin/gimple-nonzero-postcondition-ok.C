// gimple_object_address_plugin.cc: item 6's own shape for nonzero-ness
// -- make_count()'s declared postcondition unconditionally guarantees
// 'r != 0' for its own return value, read declaratively off make_
// count()'s own FUNCTION_DECL (call_postcondition_guarantees_nonzero_p).
// provable_nonzero_p recognizes 'n's own def-stmt as exactly this shape
// (a GIMPLE_CALL to a callee whose postcondition names its own result
// identifier), so consumer(n)'s own obligation is discharged purely
// from that declared guarantee. See ~/gimple-contract-analysis.md.
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

int make_count () post<conveyor_ctrl_v>(r: r != 0) { return 5; }

int consumer (int n) pre<conveyor_ctrl_v>(n != 0) { return 10 / n; }

int h () { int n = make_count (); return consumer (n); }

int main () { return h () - 2; }
