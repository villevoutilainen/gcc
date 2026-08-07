// D4324: -fcontract-symbolic-proofs, nonzero-ness's own OA_UNKNOWN case
// for a symbolic precondition -- 'm' is a plain, unconstrained integer
// parameter with no established fact of any kind (no direct nz-fact, no
// exclude-zero range, no conveyor-side m_nz_map/m_range_map fact
// either), so there is nothing for consumer's own precondition
// obligation to consult.  The sound answer is "cannot verify," not
// silent acceptance.  See .claude/plans/well-we-last-discussed-ethereal-
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

int consumer (int n) pre<symbolic_ctrl_v>(n != 0) { return 10 / n; }

int relay (int m)
{
  return consumer (m); // { dg-warning "cannot verify" }
}

int main ()
{
  return relay (5) - 2;
}
