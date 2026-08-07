// Axiom contracts (~/gcc-axiom-contracts.md): -fcontract-symbolic-proofs,
// a function's *own* precondition establishing a ptr->field range fact
// (m_contract_field_range_map) for the rest of its own body -- g's own
// precondition "this->count >= 40 && this->count < 100" is trusted for
// its own object, so the consume_count () call inside g's own body can
// prove consume_count's own (wider) precondition on the same field,
// entirely from g's own body walk (g's own env starts fresh per
// function, so without this self-trust the consume_count () call below
// would have nothing to consult at all).  t.produce_count()/t.g() at
// the call site is the already-established postcondition-at-call-site
// capability; what's new here is strictly the consume_count () call
// *inside* g's own body.  See .claude/plans/well-we-last-discussed-
// ethereal-duckling.md.
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

struct thing {
  int count;
  void produce_count ()
    post<symbolic_ctrl_v>(this->count >= 40 && this->count < 100)
  { count = 55; }
  void consume_count ()
    pre<symbolic_ctrl_v>(this->count >= 20 && this->count < 1000)
  { }
  void g ()
    pre<symbolic_ctrl_v>(this->count >= 40 && this->count < 100)
  {
    consume_count ();
  }
};

int main ()
{
  thing t;
  t.produce_count ();
  t.g ();
  return 0;
}
