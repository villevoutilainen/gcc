// D4324 Commit 2: the built-in GIMPLE pass's own mirror of d4324-
// conveyor-relational-arithmetic-shift.C -- cg_get_relational's own
// PLUS_EXPR/MINUS_EXPR-by-constant transfer, and cg_offset_compatible_
// with_code's own sign check at consult time.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }

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

int f (int n, int const q) pre<ctrl_v> (n < q) { return n; }

int use_unsound (int x, int const q) pre<ctrl_v> (x < q)
{
  int j = x + 1;
  return f (j, q); // { dg-warning "cannot verify that .j. satisfies" }
}

int use_sound (int x, int const q) pre<ctrl_v> (x < q)
{
  int j = x - 1;
  return f (j, q);
}

int main () { return use_unsound (2, 5) + use_sound (2, 5); }
