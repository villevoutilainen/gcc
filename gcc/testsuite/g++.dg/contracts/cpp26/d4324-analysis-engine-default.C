// D4324: without -fcontract-analysis-engine=, the AST engine reports
// the analysis diagnostics, including the ones only it performs.
// Counterpart of d4324-analysis-engine-gimple.C.
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

int f () conveyor
{
  int arr[3] = {1, 2, 3};
  return arr[5]; // { dg-error "array index .5. out of bounds" }
}

int g (int n) conveyor
{
  return 10 / n; // { dg-error "divisor .n. not provably nonzero" }
}

struct S {
  int v;
  void check () { contract_assert<plain_ctrl_v>(std::is_object_address(this)); } // { dg-error "may only be used inside a conveyor- or symbolic-checked predicate" }
};

int main () { S s{1}; s.check (); return f () + g (1); }
