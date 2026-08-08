// D4324: the self-trust case for a relational precondition -- g's own
// declared precondition "x < q" is trusted unconditionally for the
// rest of g's own body (oa_establish_shared_substrate_self_trust's new
// relational loop), so forwarding both parameters unchanged to f's own
// identically-shaped precondition ('f(x, q)') is discharged purely by
// matching the established (x, LT_EXPR, q) fact against the
// substituted (x, q) pair -- never resolving either parameter to any
// numeric value. See .claude/plans/well-we-last-discussed-ethereal-
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

int f (int x, int const q) pre<ctrl_v> (x < q) post<ctrl_v> (r: r < q) { return x; }
int g (int x, int const q) pre<ctrl_v> (x < q) { return f (x, q); }

int main () { return g (2, 5) - 2; }
