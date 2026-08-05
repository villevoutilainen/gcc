// Axiom contracts (~/gcc-axiom-contracts.md): -fcontract-symbolic-proofs
// extended to cover the bare-scalar shape Mechanism B already verifies at
// runtime (post<ctrl>(r: ...) on a return-value binder, pre<ctrl>(x: ...)
// on a by-value parameter) -- established [40,100) is fully subsumed by
// required [20,1000), so the obligation is discharged silently, entirely
// at compile time, with no runtime check involved at all.
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

int producer () post<symbolic_ctrl_v>(r: r >= 40 && r < 100) { return 55; }
void consumer (int x) pre<symbolic_ctrl_v>(x >= 20 && x < 1000) { (void) x; }

int main ()
{
  int y = producer ();
  consumer (y);
  return 0;
}
