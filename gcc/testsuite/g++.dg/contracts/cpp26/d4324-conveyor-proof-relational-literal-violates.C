// D4324/P2680: -fcontract-conveyor-proofs, oa_env_check_relational_fact_1's
// own literal-vs-literal fast path (both call arguments are compile-time
// literals) hitting OA_PROVEN_FALSE for a param-vs-param relational
// precondition -- the AST-walk analogue of d4324-gimple-relational-
// literal-violates.C. Exercises the established-fact follow-up note's
// own literal branch specifically (see
// .claude/plans/lazy-stirring-pearl.md).
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

int check (int x, int q) pre<conveyor_ctrl_v>(x < q) { return x + q; }

int caller ()
{
  return check (5, 3); // { dg-error "provably violates the precondition" }
                        // { dg-message "is established \[^\n\]*, but the precondition requires" "established fact" { target *-*-* } .-1 }
}

int main () { return 0; }
