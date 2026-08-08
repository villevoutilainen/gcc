// The built-in GIMPLE-pass engine's own item-6 shape for nonzero-ness
// (-fcontract-conveyor-proofs-gimple): make_count()'s declared
// postcondition unconditionally guarantees 'r != 0' for its own return
// value, read declaratively off make_count()'s own FUNCTION_DECL
// (cg_call_postcondition_guarantees_p). cg_provable_nonzero_p
// recognizes 'n's own def-stmt as exactly this shape (a GIMPLE_CALL to
// a callee whose postcondition names its own result identifier), so
// consumer(n)'s own obligation is discharged purely from that declared
// guarantee. See gcc/cp/contracts-gimple.cc and
// ~/gimple-contract-analysis.md.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }
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
