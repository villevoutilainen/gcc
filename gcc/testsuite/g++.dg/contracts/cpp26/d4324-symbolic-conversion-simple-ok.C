// D4324: -fcontract-symbolic-proofs' own axis of the same conversion-
// lookthrough covered for -fcontract-conveyor-proofs by
// d4324-conveyor-conversion-simple-ok.C -- oa_handle_precondition_
// simple_range_obligation (shared between the conveyor and symbolic
// flavors) recognizes a class-typed parameter reaching a scalar
// comparison via its own conversion operator the same way for both. f's
// own precondition "q < 5" self-trust-seeds a range fact for q; that
// fact is then consulted when f calls need_small (q), whose own
// precondition requires "x < 10" -- provable since [., 5) is a subset
// of [., 10), entirely via the class-typed parameter's own conversion,
// never resolving wrap's value from its type (the call inside f's own
// body is the mechanism under test, and must be silent). main's own
// call to f is the unrelated, already-established boundary case (a
// fresh, class-typed argument with no decl or fact behind it at all --
// see d4324-conveyor-conversion-unknown-boundary.C) and is expected to
// warn, exactly mirroring d4324-conveyor-conversion-simple-ok.C's own
// identical boundary case. See .claude/plans/well-we-last-discussed-
// ethereal-duckling.md.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

struct wrap {
  int v;
  constexpr wrap (int v_) : v (v_) {}
  constexpr operator int () const { return v; }
};

int need_small (int x) pre<symbolic_ctrl_v> (x < 10) { return x; }

int f (wrap q) pre<symbolic_ctrl_v> (q < 5)
{
  return need_small (q);
}

int main ()
{
  return f (wrap (2)); // { dg-warning "cannot verify" }
}
