// D4324: item 6 for relational facts must still respect Rule 1
// invalidation -- 'y = y + 1;' between make_val's own postcondition
// establishing "y < q" and the call to consumer must invalidate that
// fact for the OLD value of y, so this must report "cannot verify".
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

int make_val (int x, int const q) pre<ctrl_v> (x < q) post<ctrl_v> (r: r < q) { return x; }
int consumer (int y, int const q) pre<ctrl_v> (y < q) { return y; }

int caller (int x, int const q) pre<ctrl_v> (x < q)
{
  int y = make_val (x, q);
  y = y + 1;
  return consumer (y, q); // { dg-warning "cannot verify that .y. satisfies the precondition" }
}

int main () { return caller (2, 5) - 3; }
