// D4324: a relational precondition's own literal-vs-literal case where
// the comparison provably does NOT hold at this specific call site --
// a hard error (oa_relational_literal_holds returning false), not
// merely "cannot verify", exactly mirroring the existing literal-range
// obligation's own OA_PROVEN_FALSE path. See .claude/plans/well-we-
// last-discussed-ethereal-duckling.md.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

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
inline constexpr conveyor_ctrl ctrl_v{};

int f (int x, int const q) pre<ctrl_v> (x < q) { return x; }

int caller () { return f (5, 2); } // { dg-error "provably violates the precondition" }

int main () { return 0; }
