// Axiom contracts (~/gcc-axiom-contracts.md): -fcontract-symbolic-proofs,
// bare-scalar shape, the partial-overlap case a numeric range has that a
// boolean predicate fact doesn't -- established [10,30) partially
// overlaps required [20,1000): some established values would satisfy the
// precondition and some wouldn't, so this is neither a proof of success
// nor a proof of violation, just "cannot verify" (not a false claim of a
// proven violation).
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

int producer_partial () post<symbolic_ctrl_v>(r: r >= 10 && r < 30) { return 25; }
void consumer (int x) pre<symbolic_ctrl_v>(x >= 20 && x < 1000) { (void) x; }

int main ()
{
  int y = producer_partial ();
  consumer (y); // { dg-warning "cannot verify" }
  return 0;
}
