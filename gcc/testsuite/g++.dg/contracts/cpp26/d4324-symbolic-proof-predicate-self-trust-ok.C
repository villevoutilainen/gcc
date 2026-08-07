// Axiom contracts (~/gcc-axiom-contracts.md): -fcontract-symbolic-proofs,
// a function's *own* precondition establishing a shared-substrate fact
// for the rest of *its own body* -- g()'s precondition "is_opened
// (this)" is trusted (the same "assume your own non-ignored
// precondition holds" model the classic is_object_address/nonzero/
// range facts already use), so the read () call inside g()'s own body
// can prove read()'s own precondition of the same shape, entirely from
// g's own body walk -- g's own env starts fresh per function, so
// without this self-trust the read () call below would have nothing
// to consult at all.  f.open()/f.g() at the call site is the already-
// established postcondition-at-call-site capability; what's new here
// is strictly the read () call *inside* g's own body.  See
// .claude/plans/well-we-last-discussed-ethereal-duckling.md.
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

struct io_facility {
  static bool is_opened (io_facility*) symbolic;
  void open () post<symbolic_ctrl_v>(is_opened (this)) {}
  void read () pre<symbolic_ctrl_v>(is_opened (this)) {}
  void g () pre<symbolic_ctrl_v>(is_opened (this))
  {
    read ();
  }
};

int main ()
{
  io_facility f;
  f.open ();
  f.g ();
  return 0;
}
