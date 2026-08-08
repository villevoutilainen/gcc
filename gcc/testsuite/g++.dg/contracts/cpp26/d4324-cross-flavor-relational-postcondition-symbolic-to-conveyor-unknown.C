// D4324: one-way trust for a relational fact established via item 6
// -- make_val_symbolic's own SYMBOLIC-flavored postcondition
// establishes "y < q" for the assigned-to decl (tagged NOT conveyor-
// established); consumer_conveyor's own CONVEYOR-flavored precondition
// requires the same relation -- a symbolic-established fact must
// never satisfy a conveyor obligation, so this must report "cannot
// verify". See .claude/plans/well-we-last-discussed-ethereal-
// duckling.md.
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

int make_val_symbolic (int x, int const q) pre<symbolic_ctrl_v> (x < q)
  post<symbolic_ctrl_v> (r: r < q) { return x; }
int consumer_conveyor (int y, int const q) pre<ctrl_v> (y < q) { return y; }

int caller (int x, int const q) pre<symbolic_ctrl_v> (x < q)
{
  int y = make_val_symbolic (x, q);
  return consumer_conveyor (y, q); // { dg-warning "cannot verify that .y. satisfies the precondition" }
}

int main () { return caller (2, 5) - 2; }
