// The built-in GIMPLE-pass engine's own version of the item-6
// mismatch case (-fcontract-conveyor-proofs-gimple): make_val's own
// postcondition establishes "y < q" for the assigned-to SSA name, but
// consumer's own precondition compares y against a DIFFERENT
// parameter -- must report "cannot verify".
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }
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

int make_val (int x, int const q) pre<ctrl_v> (x < q) post<ctrl_v> (r: r < q) { return x; }
int consumer (int y, int const other) pre<ctrl_v> (y < other) { return y; }

int caller (int x, int const q, int const other) pre<ctrl_v> (x < q)
{
  int y = make_val (x, q);
  return consumer (y, other); // { dg-warning "cannot verify that .y. satisfies the precondition" }
}

int main () { return caller (2, 5, 10) - 2; }
