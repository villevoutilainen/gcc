// Axiom contracts (~/gcc-axiom-contracts.md): -fcontract-symbolic-proofs,
// ptr->field shape, OA_PROVEN_FALSE case -- produce_bad()'s established
// range for this->count, [-100,-50), is fully disjoint from
// consume_count()'s required [20,1000): a genuine, provable violation,
// entirely at compile time.
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

struct thing {
  int count;
  void produce_bad ()
    post<symbolic_ctrl_v>(this->count >= -100 && this->count < -50)
  { count = -60; }
  void consume_count ()
    pre<symbolic_ctrl_v>(this->count >= 20 && this->count < 1000)
  { }
};

int main ()
{
  thing t;
  t.produce_bad ();
  t.consume_count (); // { dg-error "provably violates the precondition" }
  return 0;
}
