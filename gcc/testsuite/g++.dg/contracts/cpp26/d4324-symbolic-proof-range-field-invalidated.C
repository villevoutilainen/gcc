// Axiom contracts (~/gcc-axiom-contracts.md): -fcontract-symbolic-proofs,
// ptr->field shape -- a direct field write ('t.count = 5;') invalidates
// just that one field's own established range, not the whole object's:
// consume_both()'s own combined precondition (checked in a single call,
// so no *other* contracted call's own implicit 'this' argument gets a
// chance to separately, conservatively invalidate everything first --
// see this new shape's own established-vs-runtime parity report) sees
// 'count' as no longer verifiable, while 'other' -- untouched -- is still
// established and subsumed, silently discharged.
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
  void consume_both ()
    pre<symbolic_ctrl_v>(this->count >= 20 && this->count < 1000
			  && this->other >= 0 && this->other < 20)
  { }
};

int main ()
{
  thing t;
  t.produce_both ();
  t.count = 5;
  t.consume_both (); // { dg-warning "cannot verify that field 'thing::count'" }
  return 0;
}
