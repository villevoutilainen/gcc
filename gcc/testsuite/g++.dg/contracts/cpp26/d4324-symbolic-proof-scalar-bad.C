// Axiom contracts (~/gcc-axiom-contracts.md): -fcontract-symbolic-proofs,
// bare-scalar shape, OA_PROVEN_FALSE case -- producer_neg()'s established
// range [-100,-50) is fully disjoint from consumer()'s required [20,1000):
// no possible value can satisfy both, a genuine, provable violation, all
// at compile time.
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

int producer_neg () post<symbolic_ctrl_v>(r: r >= -100 && r < -50) { return -60; }
void consumer (int x) pre<symbolic_ctrl_v>(x >= 20 && x < 1000) { (void) x; }

int main ()
{
  int y = producer_neg ();
  consumer (y); // { dg-error "provably violates the precondition" }
  return 0;
}
