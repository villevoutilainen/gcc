// D4324 Commit 2: tracking a plain relational fact ("paramA OP paramB")
// through constant-offset arithmetic ('j = x +/- k;'). Both directions
// exercised: incrementing a *tight* bound by 1 correctly reports
// unverifiable (x could equal q - 1, making x + 1 == q), while
// decrementing it correctly verifies (x - 1 < q - 1, which still
// implies x - 1 < q).
// { dg-do compile { target c++26 } }
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
