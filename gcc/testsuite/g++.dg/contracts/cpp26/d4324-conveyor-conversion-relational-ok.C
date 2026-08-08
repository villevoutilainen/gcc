// D4324: a relational precondition ("paramA OP paramB",
// oa_match_comparison_against_param in contracts.cc) where *both*
// sides are class-typed parameters, each reached via its own implicit
// conversion operator -- oa_underlying_param_operand (via oa_strip_
// conversion_call) unwraps both independently, and neither side's
// value is ever resolved. g's own precondition "x < q" self-trust-
// seeds a relational fact between its own x/q; calling f (x, q)
// substitutes a by-value copy of each (materialized as a TARGET_EXPR,
// also handled by oa_strip_conversion_call), so the obligation is
// discharged purely by matching identity, not value (that call inside
// g's own body is the mechanism under test, and must be silent).
// main's own call to g is the unrelated, already-established boundary
// case (fresh, class-typed arguments with no decl or fact behind them
// at all -- see d4324-conveyor-conversion-unknown-boundary.C) and is
// expected to warn. See .claude/plans/well-we-last-discussed-
// ethereal-duckling.md.
// { dg-do run { target c++26 } }
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

struct wrap {
  int v;
  constexpr wrap (int v_) : v (v_) {}
  constexpr operator int () const { return v; }
};

int f (wrap x, wrap q) pre<ctrl_v> (x < q) { return x; }

int g (wrap x, wrap q) pre<ctrl_v> (x < q)
{
  return f (x, q);
}

int main () { return g (wrap (2), wrap (5)) - 2; } // { dg-warning "cannot verify" }
