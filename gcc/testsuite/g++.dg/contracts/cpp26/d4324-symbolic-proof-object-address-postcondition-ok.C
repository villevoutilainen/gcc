// D4324: -fcontract-symbolic-proofs, the postcondition-at-call-site
// counterpart of the self-trust case -- acquire()'s own symbolic
// postcondition "is_object_address(this)" is recorded into the new
// symbolic-only m_symbolic_object_address_map, keyed by the caller's own
// substituted 'this' argument.  Calling through a *pointer* receiver
// ('hp->acquire()') deliberately, rather than a plain object
// ('h.acquire()'): a plain-object receiver's own 'this' substitutes to
// '&h', which oa_provable_p already treats as trivially true regardless
// of any fact tracking (any address-of-a-decl is definitionally an
// object address) -- not a meaningful exercise of the new map at all.
// A pointer receiver's own 'this' substitutes to the bare pointer
// variable itself ('hp'), which has no such trivial shortcut, so
// use()'s own precondition obligation genuinely depends on the fact
// this pass records at the acquire() call site.  See .claude/plans/
// well-we-last-discussed-ethereal-duckling.md.
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

struct handle {
  void acquire () post<symbolic_ctrl_v>(std::is_object_address (this)) {}
  void use () pre<symbolic_ctrl_v>(std::is_object_address (this)) {}
};

int main ()
{
  handle h;
  handle *hp = &h;
  hp->acquire ();
  hp->use ();
  return 0;
}
