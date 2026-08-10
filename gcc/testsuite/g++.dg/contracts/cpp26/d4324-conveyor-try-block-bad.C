// D4324: oa_walk_stmt had no case for TRY_BLOCK either (the same class
// of gap as CLEANUP_STMT, see d4324-conveyor-cleanup-stmt-bad.C) -- a
// real, user-written 'try { ... } catch (...) { ... }', not just the
// compiler-generated TRY_FINALLY_EXPR shape already handled. With no
// case, execution fell to the default fallback, silently skipping the
// try body, every handler, and anything following the whole construct.
// Confirmed via direct testing that this exact 'need_small (20)' call
// -- a provable, unconditional precondition violation -- produced no
// diagnostic at all once wrapped in a bare try/catch; the walk simply
// never reached it. See .claude/plans/well-we-last-discussed-ethereal-
// duckling.md.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

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

int need_small (int x) pre<conveyor_ctrl_v> (x < 10) { return x; }

int main ()
{
  try
    {
      return need_small (20); // { dg-error "provably violates the precondition" }
    }
  catch (...)
    {
      return 1;
    }
}
