// Axiom contracts (~/gcc-axiom-contracts.md): -fcontract-symbolic-proofs,
// predicate-chaining proof on a callee's own return value, OA_PROVEN_
// FALSE case -- produce_bad's postcondition guarantees "!check_it (r)",
// the exact opposite polarity of what consume's precondition requires
// for that same value.  Closes the same cross-combination test gap as
// d4324-symbolic-proof-predicate-returnvalue-ok.C, for the violation
// direction.  See .claude/plans/well-we-last-discussed-ethereal-
// duckling.md.
// { dg-do compile { target c++26 } }
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

int produce_bad () post<symbolic_ctrl_v>(r: !check_it (r)) { return -1; }
void consume (int x) pre<symbolic_ctrl_v>(check_it (x)) { (void) x; }

void caller ()
{
  int r = produce_bad ();
  consume (r); // { dg-error "provably violates the precondition" }
}

int main () { caller (); return 0; }
