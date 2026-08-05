// Axiom contracts (~/gcc-axiom-contracts.md): -fcontract-symbolic-proofs,
// bare-scalar shape, OA_UNKNOWN case -- 'y' was never established via a
// call whose postcondition asserts a range for its own return-value
// binder (just a plain literal assignment), so there is no compile-time
// fact to check the precondition against either way.  The sound answer
// is "cannot verify," not silent acceptance.
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

void consumer (int x) pre<symbolic_ctrl_v>(x >= 20 && x < 1000) { (void) x; }

int main ()
{
  int y = 55;
  consumer (y); // { dg-warning "cannot verify" }
  return 0;
}
