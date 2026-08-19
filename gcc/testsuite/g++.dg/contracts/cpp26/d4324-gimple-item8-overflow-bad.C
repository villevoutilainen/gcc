// Companion to d4324-gimple-item8-overflow-ok.C: unconstrained operands,
// for addition, negation, and multiplication. Rejected by contracts.cc's
// own mandatory, unconditional item 8 pass regardless of any GIMPLE flag
// (confirmed by direct testing: the identical errors fire even with
// -fcontract-conveyor-proofs-gimple entirely absent) -- so, exactly like
// item 8's own div/mod violation tests, this can only demonstrate
// "compilation still correctly rejects this with both engines active,"
// not GIMPLE's own check in isolation.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

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

int plus_bad (int a, int b) conveyor
{
  return a + b; // { dg-error "not provably free of overflow" }
}

int negate_bad (int a) conveyor
{
  return -a; // { dg-error "not provably free of overflow" }
}

int mult_bad (int a, int b) conveyor
{
  return a * b; // { dg-error "not provably free of overflow" }
}

int main () { return 0; }
