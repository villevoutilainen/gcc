// D4324/P2680 item 8, Increment V: a fact established by ordinary code
// *before* a contract_assert (here, via the existing if/else merge
// straight-line tracking) is correctly available to that
// contract_assert's own div/mod scan.
// { dg-do run { target c++26 } }
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
  int m;
  if (n != 0)
    m = n;
  else
    m = 1;
  contract_assert<conveyor_ctrl_v>(10 / m > 0);
  return 0;
}

int main () { return f (5); }
