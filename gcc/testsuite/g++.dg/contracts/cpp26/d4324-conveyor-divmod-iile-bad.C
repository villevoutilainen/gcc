// D4324/P2680 item 8, Increment E-divmod: the same by-reference
// capture-proxy redirect must still correctly reject a div/mod whose
// divisor (an ordinary parameter, captured by reference) has no
// provable nonzero-ness at all.
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

int f (int q) conveyor
{
  int a = 1;
  int* p = [&]() { int unused = 10 / q; return &a; }(); // { dg-error "divisor .q. not provably nonzero in a conveyor function" }
  contract_assert<conveyor_ctrl_v>(std::is_object_address(p));
  return *p;
}

int main () { int x = 1; return f (x) - 1; }
