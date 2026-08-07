// D4324: -fcontract-symbolic-proofs, is_object_address's own OA_UNKNOWN
// case for a symbolic precondition -- 'q' is a plain, unconstrained
// pointer parameter with no established fact of any kind (no self-
// trust, no postcondition-at-call-site establishment, no conveyor-side
// m_map fact either), so there is nothing for consumer's own precondition
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

void consumer (int *q) pre<symbolic_ctrl_v>(std::is_object_address (q))
{
  (void) q;
}

void relay (int *q)
{
  consumer (q); // { dg-warning "cannot verify" }
}

int main ()
{
  int x = 5;
  relay (&x);
  return 0;
}
