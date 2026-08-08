// The built-in GIMPLE-pass engine's own one-way trust for a
// relational fact established via item 6 -- make_val's own CONVEYOR-
// flavored postcondition establishes "y < q"; consumer_symbolic's own
// SYMBOLIC-flavored precondition requires the same relation -- a
// conveyor-established fact is trustworthy enough for symbolic's own
// check to rely on, so this is discharged silently.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple -fcontract-symbolic-proofs-gimple" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

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

int make_val (int x, int const q) pre<ctrl_v> (x < q) post<ctrl_v> (r: r < q) { return x; }
int consumer_symbolic (int y, int const q) pre<symbolic_ctrl_v> (y < q) { return y; }

int caller (int x, int const q) pre<ctrl_v> (x < q)
{
  int y = make_val (x, q);
  return consumer_symbolic (y, q);
}

int main () { return caller (2, 5) - 2; }
