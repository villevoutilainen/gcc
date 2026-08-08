// D4324: cross-flavor trust (d4324-cross-flavor-relational-conveyor-
// to-symbolic-ok.C's own scenario) for a *relational* fact between two
// class-typed parameters, each reached via its own conversion
// operator. g_conveyor's own CONVEYOR-flavored precondition
// self-trust-seeds "x < q" as conveyor-established; f_symbolic's own
// SYMBOLIC-flavored precondition requires the same relation on the
// same forwarded (by-value-copied, TARGET_EXPR-wrapped) pair -- a
// conveyor-established fact is trustworthy enough for symbolic's own
// check to rely on, discharged silently (no warning at that call).
// main's own call to g_conveyor is the unrelated, already-established
// boundary case (a fresh, class-typed argument with no decl or fact
// behind it at all -- see d4324-conveyor-conversion-unknown-
// boundary.C) and is expected to warn. See .claude/plans/well-we-
// last-discussed-ethereal-duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -fcontract-symbolic-proofs" }

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

int f_symbolic (wrap x, wrap q) pre<symbolic_ctrl_v> (x < q) { return x; }
int g_conveyor (wrap x, wrap q) pre<ctrl_v> (x < q) { return f_symbolic (x, q); }

int main ()
{
  wrap a (2), b (5);
  return g_conveyor (a, b) - 2; // { dg-warning "cannot verify" }
}
