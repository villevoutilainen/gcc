// D4324: nonzero-ness proven via an established, exclude-zero *range*
// fact rather than a direct 'n != 0' fact -- make_count()'s symbolic
// postcondition establishes n's range as [1, 100) in
// m_contract_scalar_range_map (the existing bare-scalar shape,
// Mechanism B's own static-prover analogue), and the new nonzero-
// conjunct consult in oa_handle_call_symbolic_precondition_obligation
// recognizes that a range excluding zero (here, entirely positive) is
// just as good as a direct nz-fact.  See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
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

int make_count () post<symbolic_ctrl_v>(r: r >= 1 && r < 100) { return 5; }

int consumer (int n) pre<symbolic_ctrl_v>(n != 0) { return 10 / n; }

int g ()
{
  int n = make_count ();
  return consumer (n);
}

int main ()
{
  return g () - 2;
}
