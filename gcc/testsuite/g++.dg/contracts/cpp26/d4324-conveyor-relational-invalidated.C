// D4324: a relational precondition's own established fact must be
// invalidated by a reassignment of either side (Rule 1's own whole-
// object invalidation, extended to oa_env::relational_invalidate_
// involving) -- 'x = x + 1;' between g's own self-trusted "x < q" and
// the call to f must invalidate the fact recorded for the OLD value of
// x, so this must report "cannot verify", not silently reuse a fact
// about a value x no longer holds. See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
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
int g (int x, int const q) pre<ctrl_v> (x < q)
{
  x = x + 1;
  return f (x, q); // { dg-warning "cannot verify that .x. satisfies the precondition" }
}

int main () { return g (2, 5) - 3; }
