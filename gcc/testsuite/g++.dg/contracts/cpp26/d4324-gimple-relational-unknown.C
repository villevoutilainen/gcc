// The built-in GIMPLE-pass engine's own version of the reported gap
// this feature closes (-fcontract-conveyor-proofs-gimple): g's own
// precondition only guarantees "x < 7" (its own bound), but calling
// f(x) relies on f's own default argument for its second parameter, a
// value entirely unrelated to g's own q -- no established relational
// fact to consult, so this must report "cannot verify".
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

int f (int x, int const q = 5) pre<ctrl_v> (x < q) post<ctrl_v> (r: r < q) { return x; }
int g (int x, int const q = 7) pre<ctrl_v> (x < q) post<ctrl_v> (r: r < q)
{
  return f (x); // { dg-warning "cannot verify that .x. satisfies the precondition" }
}

int main () { return g (2) - 2; }
