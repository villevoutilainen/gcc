// D4324: the same check as d4324-conveyor-assert-scalar-range-bad.C,
// but for -fcontract-symbolic-proofs and a custom is_symbolic control
// type (matching the originally reported repro exactly).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct symbolic_ctrl {
  static constexpr bool is_ignored (const sc::assertion_static_info) { return false; }
  static constexpr bool constify (const sc::assertion_static_info) { return false; }
  static constexpr bool assumable (const sc::assertion_static_info) { return false; }
  static constexpr bool is_symbolic (const sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context&) const { }
};

int
main ()
{
  int x = 42;
  x = 172;
  contract_assert<symbolic_ctrl()>(x < 30); // { dg-error "condition .*x < 30.* is provably false" }
  return 0;
}
