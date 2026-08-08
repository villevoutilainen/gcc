// D4324: one-way trust between the two control-object flavors, for
// relational facts -- a symbolic-established relational fact is
// purely trusted, never verified against anything, so it must never
// satisfy a *conveyor* obligation -- that would silently weaken the
// guarantee conveyor is supposed to provide. g_symbolic's own SYMBOLIC-
// flavored precondition establishes "x < q" (self-trust, tagged NOT
// conveyor-established); f_conveyor's own CONVEYOR-flavored
// precondition requires the same relation on the same forwarded pair
// -- must report "cannot verify", not silently pass. See .claude/
// plans/well-we-last-discussed-ethereal-duckling.md.
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

int f_conveyor (int x, int const q) pre<ctrl_v> (x < q) { return x; }
int g_symbolic (int x, int const q) pre<symbolic_ctrl_v> (x < q)
{
  return f_conveyor (x, q); // { dg-warning "cannot verify that .x. satisfies the precondition" }
}

int main () { return g_symbolic (2, 5) - 2; }
