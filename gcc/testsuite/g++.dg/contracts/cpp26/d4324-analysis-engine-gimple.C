// D4324: -fcontract-analysis-engine=gimple leaves the analysis
// diagnostics to the GIMPLE engine.  The AST engine's own are silenced,
// except for overflow within a contract's own condition, which the AST
// engine still reports: the front end folds some of the arithmetic a
// condition is written with ('x + 1 > 0' is 'x >= 0') before the GIMPLE
// engine sees it.  A check both engines perform (divisors) is reported
// once, and a rule about where a construct may appear is still
// reported.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

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

int f (int x) conveyor
  pre<std::contracts::conveyor_assert_v> (x + 1 > 0) // { dg-error "result of .\\(x \\+ 1\\). not provably free of overflow" }
{
  return x;
}

int g (int n) conveyor
{
  return 10 / n; // { dg-error "divisor .n. not provably nonzero" }
}

struct S {
  int v;
  void check () { contract_assert<plain_ctrl_v>(std::is_object_address(this)); } // { dg-error "may only be used inside a conveyor- or symbolic-checked predicate" }
};

int main () { S s{1}; s.check (); return f (1) + g (1); }
