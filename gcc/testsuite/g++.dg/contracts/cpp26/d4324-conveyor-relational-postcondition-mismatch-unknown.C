// D4324: item 6 for relational facts, the mismatch case -- make_val's
// own postcondition establishes "y < q" for the assigned-to decl, but
// consumer's own precondition compares y against a DIFFERENT
// parameter ('other', not q) -- the established fact's own RHS (q)
// doesn't match the substituted RHS (other), so this must report
// "cannot verify". See .claude/plans/well-we-last-discussed-ethereal-
// duckling.md.
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

int make_val (int x, int const q) pre<ctrl_v> (x < q) post<ctrl_v> (r: r < q) { return x; }
int consumer (int y, int const other) pre<ctrl_v> (y < other) { return y; }

int caller (int x, int const q, int const other) pre<ctrl_v> (x < q)
{
  int y = make_val (x, q);
  return consumer (y, other); // { dg-warning "cannot verify that .y. satisfies the precondition" }
}

int main () { return caller (2, 5, 10) - 2; }
