// D4324/P2680 item 8, Increment E-divmod: the "provably nonzero" fact
// map's by-reference lambda-capture-proxy redirect, exercised by a
// div/mod check reached inside an invoked closure's own body (the
// closure body only gets walked at all here because the outer
// contract_assert queries is_object_address on its return value --
// see the plan's note on this being a real, separate, still-open
// coverage gap for div/mod-only closures with no is_object_address
// consumer at all).
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

int f () conveyor
{
  int b = 5;
  int a = 1;
  int* p = [&]() { int unused = 10 / b; return &a; }();
  contract_assert<conveyor_ctrl_v>(std::is_object_address(p));
  return *p;
}

int main () { return f () - 1; }
