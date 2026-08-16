// D4324: never_proven, symbolic flavor -- same exemption as
// d4324-never-proven-conveyor-ok.C, demonstrated here on a hand-rolled
// control object combining is_symbolic and never_proven (no built-in
// "never_proven_symbolic" object ships, since never_proven is an
// independent trait orthogonal to conveyor/symbolic, not a fourth
// flavor of its own -- see <contracts>'s own documentation).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct never_proven_symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  static constexpr bool never_proven (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr never_proven_symbolic_ctrl never_proven_symbolic_ctrl_v{};

bool is_opened (int*) symbolic;

int
f (int* p)
{
  // is_opened's own truth is never established anywhere -- an ordinary
  // analyzed_symbolic/proven_symbolic contract would warn or error;
  // never_proven produces no diagnostic at all.
  contract_assert<never_proven_symbolic_ctrl_v>(is_opened (p));
  return 0;
}

int main () { int x = 0; return f (&x); }
