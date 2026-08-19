// Item 8 (overflow) mandatory UB scan, GIMPLE side, "Increment 1"
// (numeric-only route -- NEGATE_EXPR and binary PLUS_EXPR/MINUS_EXPR/
// MULT_EXPR; the AST engine's own "type-bound witness" rescue for unit
// shifts is not yet ported, see cg_check_overflow_ub's own leading
// comment). Also also caught by contracts.cc's own mandatory,
// unconditional item 8 pass (gated purely on the enclosing function
// being conveyor, no proof flag needed), so this only demonstrates
// "compilation succeeds with both engines active," matching item 7/8's
// own documented testing limitation -- see this directory's item7 and
// item8-divmod tests.
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

int plus_ok (int a, int b) conveyor
  pre<conveyor_ctrl_v>(a >= 0 && a <= 100)
  pre<conveyor_ctrl_v>(b >= 0 && b <= 100)
{
  return a + b;
}

int inc_ok (int a) conveyor
  pre<conveyor_ctrl_v>(a >= 0 && a <= 100)
{
  return a + 1;
}

int negate_ok (int a) conveyor
  pre<conveyor_ctrl_v>(a >= 0 && a <= 100)
{
  return -a;
}

int mult_ok (int a, int b) conveyor
  pre<conveyor_ctrl_v>(a >= 0 && a <= 100)
  pre<conveyor_ctrl_v>(b >= 0 && b <= 100)
{
  return a * b;
}

int main ()
{
  return plus_ok (1, 2) + inc_ok (1) + negate_ok (1) + mult_ok (1, 2);
}
