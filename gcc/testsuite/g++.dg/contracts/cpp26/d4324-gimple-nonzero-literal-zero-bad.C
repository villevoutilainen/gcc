// The built-in GIMPLE-pass engine's own literal-zero sharpening for a
// nonzero-shaped conjunct ('param != 0'): a literal-zero argument's
// value is always fully known, so this is a genuine, provable violation
// -- sharpened past "cannot verify" all the way to a hard,
// unconditional "provably violates" error, mirroring contracts.cc's
// own identical literal-argument sharpening
// (oa_handle_call_conveyor_proof_obligation). Previously this engine
// only ever warned here, even for a literal-zero argument (see
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

int consumer (int n) pre<conveyor_ctrl_v>(n != 0) { return 10 / n; }

int caller ()
{
  return consumer (0); // { dg-error "provably violates the precondition" }
}

int main () { return 0; }
