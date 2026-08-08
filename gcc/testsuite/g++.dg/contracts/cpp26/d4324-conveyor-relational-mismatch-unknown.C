// D4324: a relational precondition whose established fact names the
// wrong pair -- g's own self-trust only establishes "x < q" (its own
// two parameters), but the call 'f(x, other)' substitutes f's own
// second parameter with a THIRD, unrelated parameter ('other'), not q
// -- the established fact's own RHS (q) doesn't match the substituted
// RHS (other), so this must report "cannot verify", never silently
// pass just because *some* relational fact happens to exist for x.
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

int f (int x, int const q) pre<ctrl_v> (x < q) post<ctrl_v> (r: r < q) { return x; }
int g (int x, int q, int const other) pre<ctrl_v> (x < q)
{
  return f (x, other); // { dg-warning "cannot verify that .x. satisfies the precondition" }
}

int main () { return g (2, 5, 10) - 2; }
