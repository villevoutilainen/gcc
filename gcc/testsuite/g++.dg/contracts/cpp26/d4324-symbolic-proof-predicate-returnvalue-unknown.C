// Axiom contracts (~/gcc-axiom-contracts.md): -fcontract-symbolic-proofs,
// predicate-chaining proof on a callee's own return value -- untrusted
// was never established via a call to a function whose postcondition
// asserts check_it for its own result, so the best available answer is
// "cannot verify," not silent acceptance, and not a false claim of a
// proven violation either.  Closes the same cross-combination test gap
// as d4324-symbolic-proof-predicate-returnvalue-ok.C, for the unknown
// direction.  See .claude/plans/well-we-last-discussed-ethereal-
// duckling.md.
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

void consume (int x) pre<symbolic_ctrl_v>(check_it (x)) { (void) x; }

void caller (int untrusted)
{
  consume (untrusted); // { dg-warning "cannot verify" }
}

int main () { caller (1); return 0; }
