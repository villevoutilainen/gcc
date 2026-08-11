// D4324: same rule as d4324-conveyor-callee-precondition-bad.C, for a
// contract_assert's own conveyor-flavored condition text, inside an
// otherwise-ordinary (non-conveyor) function. See .claude/plans/well-
// we-last-discussed-ethereal-duckling.md.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

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
inline constexpr conveyor_ctrl conveyor_ctrl_v{};

bool helper (int x) { return x > 0; }

void f (int x)
{
  contract_assert<conveyor_ctrl_v>(helper (x)); // { dg-error "not declared .conveyor." }
}

int main () { f (1); return 0; }
