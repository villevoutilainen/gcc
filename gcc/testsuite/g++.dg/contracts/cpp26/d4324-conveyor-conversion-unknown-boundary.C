// D4324: the deliberate boundary of oa_strip_conversion_call --
// resolving a class-typed operand down to the *decl* it's a conversion
// of (or a by-value copy of) is sound and always safe, but a genuinely
// opaque rvalue (a fresh temporary, here 'wrap (2)', with no decl or
// established fact behind it at all) must still be reported as
// unverifiable, the same way an ordinary int literal-vs-decl boundary
// already is elsewhere. This locks in that oa_strip_conversion_call
// never tries to resolve a class type's *value* from its own type
// (the approach already rejected earlier in this plan for
// integral_constant) -- only ever *which decl* an operand refers to.
// See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
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
  constexpr operator int () const conveyor { return v; }
};

int f (wrap q) pre<ctrl_v> (q < 5) { return q; }

int main () { return f (wrap (2)) - 2; } // { dg-warning "cannot verify" }
