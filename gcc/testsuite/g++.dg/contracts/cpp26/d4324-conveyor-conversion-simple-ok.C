// D4324: a scalar-comparison precondition ("param OP const",
// oa_match_simple_comparison/oa_refine_single_comparison/oa_get_range
// in contracts.cc) where the parameter is of class type and reaches
// the comparison via its own implicit conversion operator
// (oa_strip_conversion_call) rather than already being a bare
// scalar-typed decl. f's own precondition "q < 5" self-trust-seeds a
// range fact for q; that fact is then consulted when f calls
// need_small (q), whose own precondition requires "x < 10" -- provable
// since [., 5) is a subset of [., 10), entirely via the class-typed
// parameter's own conversion, never resolving wrap's value from its
// type (the call inside f's own body is the mechanism under test, and
// must be silent). main's own call to f is the unrelated, already-
// established boundary case (a fresh, class-typed argument with no
// decl or fact behind it at all -- see d4324-conveyor-conversion-
// unknown-boundary.C) and is expected to warn. See .claude/plans/
// well-we-last-discussed-ethereal-duckling.md.
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

int need_small (int x) pre<ctrl_v> (x < 10) { return x; }

int f (wrap q) pre<ctrl_v> (q < 5)
{
  return need_small (q);
}

int main () { return f (wrap (2)) - 2; } // { dg-warning "cannot verify" }
