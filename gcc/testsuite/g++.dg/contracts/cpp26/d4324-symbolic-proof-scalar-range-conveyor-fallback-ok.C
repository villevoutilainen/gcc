// D4324: symbolic's own bare-scalar range-obligation checking now falls
// back to the general-purpose m_range_map (conveyor's own numeric-
// checking substrate, populated by ordinary dataflow throughout the
// caller's body -- here, simply a literal assignment) when
// m_contract_scalar_range_map has no fact of its own for the decl --
// see oa_handle_call_symbolic_scalar_precondition_obligation's own
// comment and .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// 'y' was never established via any postcondition-bearing call (the
// only source m_contract_scalar_range_map itself has), yet the
// obligation is still discharged silently, purely from y's own,
// ordinarily-tracked literal value.
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

void consumer (int x) pre<symbolic_ctrl_v>(x >= 20 && x < 1000) { (void) x; }

int main ()
{
  int y = 55;
  consumer (y);
  return 0;
}
