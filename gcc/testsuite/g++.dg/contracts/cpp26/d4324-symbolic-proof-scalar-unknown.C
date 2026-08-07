// Axiom contracts (~/gcc-axiom-contracts.md): -fcontract-symbolic-proofs,
// bare-scalar shape, OA_UNKNOWN case -- 'y' was never established via a
// call whose postcondition asserts a range for its own return-value
// binder, nor via any dataflow conveyor's own general-purpose m_range_map
// tracking would recognize either (a plain, unconstrained parameter, not
// a literal or a comparison-refined value): oa_handle_call_symbolic_
// scalar_precondition_obligation's own m_range_map fallback (see
// .claude/plans/well-we-last-discussed-ethereal-duckling.md) has nothing
// to find here, so there is no compile-time fact to check the
// precondition against either way.  The sound answer is "cannot verify,"
// not silent acceptance.
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

void relay (int y)
{
  consumer (y); // { dg-warning "cannot verify" }
}

int main ()
{
  relay (55);
  return 0;
}
