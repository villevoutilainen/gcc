// The built-in GIMPLE-pass engine's own PROVEN_FALSE tier for a named-
// predicate fact established at the *opposite* polarity from what a
// callee's precondition requires: 'open()' establishes is_opened(this)
// true, but 'reopen()' requires '!is_opened(this)' -- a genuine,
// provable contradiction, not merely "cannot verify," mirroring
// contracts.cc's own oa_env_predicate_result/OA_PROVEN_FALSE handling
// for the identical shape. Previously this engine had no PROVEN_FALSE
// tier for this shape at all -- a contradiction and a merely-unproven
// fact were both reported the same way, as a warning (see
// .claude/plans/lazy-stirring-pearl.md, item 2.7).
// { dg-do compile }
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

struct io_facility {
  static bool is_opened (io_facility*) conveyor { return true; }
  void open () post<conveyor_ctrl_v>(is_opened (this)) {}
  void reopen () pre<conveyor_ctrl_v>(!is_opened (this)) {}
};

void contradiction_caller ()
{
  io_facility f;
  f.open ();
  f.reopen (); // { dg-error "provably violates the precondition of .*is established true, but the precondition requires it to be false" }
}

int main () { return 0; }
