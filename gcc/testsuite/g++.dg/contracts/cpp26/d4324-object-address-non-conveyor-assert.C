// D4324/P2680: std::is_object_address used inside a contract_assert
// whose control object is NOT is_conveyor() (nor is_symbolic()) is
// ill-formed, even though the argument (this) would otherwise be
// trivially provable -- the well-formedness gate is checked explicitly
// at this contract_assert's own control object, not assumed or skipped
// just because the argument looks fine.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct plain_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return false; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr plain_ctrl plain_ctrl_v{};

struct S {
  int v;
  void check () { contract_assert<plain_ctrl_v>(std::is_object_address(this)); } // { dg-error "may only be used inside a conveyor- or symbolic-checked predicate" }
};

int main () { S s{1}; s.check (); return 0; }
