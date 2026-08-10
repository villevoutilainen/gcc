// D4324: oa_walk_stmt had no case for CLEANUP_STMT -- the GENERIC node
// wrapping "the rest of this block" whenever a local variable has a
// non-trivial destructor (i.e. very commonly). With no case for it,
// execution fell to the default fallback, which only scans for stray
// is_object_address/symbolic misuse and never recurses into CLEANUP_
// BODY at all -- silently skipping every statement following such a
// declaration, for every analysis in this file (establish, consult,
// invalidate alike), not just conversion/copy-construction lookthrough.
// Confirmed via direct testing that, before this fix, this exact
// 'need_small (20)' call -- a provable, unconditional precondition
// violation -- produced *no diagnostic at all* once preceded by a
// destructor-needing local; the walk simply never reached it. Fixed by
// giving CLEANUP_STMT its own case, walking CLEANUP_BODY (the code that
// actually runs) then CLEANUP_EXPR (the destructor call itself, handled
// like any other call so Rule 2 invalidation still sees it). See
// .claude/plans/well-we-last-discussed-ethereal-duckling.md.
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

struct needs_dtor { ~needs_dtor () {} };

int need_small (int x) pre<conveyor_ctrl_v> (x < 10) { return x; }

int main ()
{
  needs_dtor guard;
  return need_small (20); // { dg-error "provably violates the precondition" }
}
