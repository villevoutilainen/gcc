// Axiom contracts (~/gcc-axiom-contracts.md): -fcontract-symbolic-proofs
// extended to cover the ptr->field shape Mechanism A already verifies at
// runtime (post<ctrl>(this->field OP const), pre<ctrl>(this->field OP
// const)) -- produce_count()'s postcondition establishes this->count in
// [40,100), consume_count()'s precondition requires this->count in
// [20,1000); [40,100) is a subset of [20,1000), so the obligation is
// discharged silently, entirely at compile time.
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
  void produce_count ()
    post<symbolic_ctrl_v>(this->count >= 40 && this->count < 100)
  { count = 55; }
  void consume_count ()
    pre<symbolic_ctrl_v>(this->count >= 20 && this->count < 1000)
  { }
};

int main ()
{
  thing t;
  t.produce_count ();
  t.consume_count ();
  return 0;
}
