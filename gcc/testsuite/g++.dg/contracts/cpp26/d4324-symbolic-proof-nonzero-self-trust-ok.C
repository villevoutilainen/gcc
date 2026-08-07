// D4324: -fcontract-symbolic-proofs, nonzero-ness now checkable for a
// symbolic-flavored precondition too (previously conveyor-only -- see
// .claude/plans/well-we-last-discussed-ethereal-duckling.md). g's own
// precondition "n != 0" is trusted for the rest of g's own body (self-
// trust, into the new symbolic-only m_symbolic_nz_map, since g's own
// contract is symbolic-, not conveyor-, active), so the consumer (n)
// call inside g's own body can prove consumer's own precondition of the
// same shape.
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

int consumer (int n) pre<symbolic_ctrl_v>(n != 0) { return 10 / n; }

int g (int n) pre<symbolic_ctrl_v>(n != 0)
{
  return consumer (n);
}

int main ()
{
  return g (5) - 2;
}
