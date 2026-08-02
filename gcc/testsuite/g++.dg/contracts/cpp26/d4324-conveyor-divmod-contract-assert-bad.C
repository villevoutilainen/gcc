// D4324/P2680 item 8, Increment V: the narrow div/mod nonzero-divisor
// dataflow check now also fires for an is_conveyor contract_assert's
// own condition, even inside an otherwise-ordinary (non-conveyor-
// qualified) function -- previously this check only ever fired inside
// a function actually declared 'conveyor'.
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
  contract_assert<conveyor_ctrl_v>(10 / n > 0); // { dg-error "divisor .n. not provably nonzero in a conveyor function" }
  return 0;
}

int main () { return f (5); }
