// D4324/P2680 item 8, Increment V: documents a deliberate, conservative
// scope decision -- the div/mod scan for a contract_assert/pre/post
// condition is checked against ENV as it stands from *prior* code only.
// A later conjunct in the *same* condition does not yet benefit from an
// earlier conjunct establishing the same fact (unlike an ordinary if/
// loop condition, which got this refinement in Increment K) -- so this
// must still be rejected, confirming the narrower scope is real and not
// silently more permissive than documented.
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

int f (int n)
{
  contract_assert<conveyor_ctrl_v>(n != 0 && 10 / n > 0); // { dg-error "divisor .n. not provably nonzero in a conveyor function" }
  return 0;
}

int main () { return f (5); }
