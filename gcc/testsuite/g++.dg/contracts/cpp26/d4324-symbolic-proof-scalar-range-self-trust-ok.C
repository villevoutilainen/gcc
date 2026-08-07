// Axiom contracts (~/gcc-axiom-contracts.md): -fcontract-symbolic-proofs,
// a function's *own* precondition establishing a bare-scalar range
// fact (m_contract_scalar_range_map) for the rest of its own body --
// g's own precondition "x >= 20 && x < 100" is trusted for its own
// parameter x, so the consumer (x) call inside g's own body can prove
// consumer's own precondition of the same range, entirely from g's own
// body walk.  See .claude/plans/well-we-last-discussed-ethereal-
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

void consumer (int x) pre<symbolic_ctrl_v>(x >= 20 && x < 100) { (void) x; }

void g (int x) pre<symbolic_ctrl_v>(x >= 20 && x < 100)
{
  consumer (x);
}

int main ()
{
  g (50);
  return 0;
}
