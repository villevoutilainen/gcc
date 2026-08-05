// Axiom contracts (~/gcc-axiom-contracts.md): -fcontract-symbolic-proofs,
// ptr->field shape, OA_UNKNOWN case -- consume_count()'s precondition
// requires this->count in a range, but nothing on this path ever called
// produce_count() (or anything else whose postcondition establishes that
// field's range), so there is no established fact to check against
// either way.
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
  t.consume_count (); // { dg-warning "cannot verify" }
  return 0;
}
