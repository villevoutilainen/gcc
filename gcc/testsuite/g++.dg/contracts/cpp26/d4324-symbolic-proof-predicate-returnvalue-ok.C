// Axiom contracts (~/gcc-axiom-contracts.md): -fcontract-symbolic-proofs,
// predicate-chaining proof on a callee's own *return value* (as opposed
// to an object identity like 'this') -- produce's postcondition
// guarantees "check_it (r)" for its own result; once assigned to a
// named decl (r = produce ()), the fact is tracked by the shared
// engine (m_predicate_fact_map) and consume's own precondition
// "check_it (x)" is discharged silently.  This exact shape (return-
// value predicate chaining) was already covered for a *conveyor*
// control object (d4324-conveyor-proof-predicate-ok.C); this is the
// same shape under a *symbolic* one, closing a cross-combination test
// gap.  See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

bool check_it (int) symbolic;

int produce () post<symbolic_ctrl_v>(r: check_it (r)) { return 1; }
void consume (int x) pre<symbolic_ctrl_v>(check_it (x)) { (void) x; }

int main ()
{
  int r = produce ();
  consume (r);
  return 0;
}
