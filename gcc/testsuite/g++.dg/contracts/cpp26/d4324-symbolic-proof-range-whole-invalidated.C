// Axiom contracts (~/gcc-axiom-contracts.md): -fcontract-symbolic-proofs,
// ptr->field shape -- reassigning the whole tracked object drops every
// one of its established fields, not just one (the same whole-object
// invalidation rule the predicate shape already has, applied here to
// this new shape's own field-range map too).
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
  int other;
  void produce_both ()
    post<symbolic_ctrl_v>(this->count >= 40 && this->count < 100
			   && this->other >= 5 && this->other < 10)
  { count = 55; other = 7; }
  void consume_count ()
    pre<symbolic_ctrl_v>(this->count >= 20 && this->count < 1000)
  { }
  void consume_other ()
    pre<symbolic_ctrl_v>(this->other >= 0 && this->other < 20)
  { }
};

int main ()
{
  thing t;
  t.produce_both ();
  t = thing ();
  t.consume_count (); // { dg-warning "cannot verify" }
  t.consume_other ();  // { dg-warning "cannot verify" }
  return 0;
}
