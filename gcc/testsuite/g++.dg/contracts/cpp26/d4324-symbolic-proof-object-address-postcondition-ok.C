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

// Needed only so ACQUIRE's own postcondition self-check below has a
// REAL, m_map-backed is_object_address(this) fact to resolve against:
// unlike a PRECONDITION's own is_object_address conjunct (always
// trusted as an axiom, oa_resolve_condition's own TRUST parameter), a
// POSTCONDITION's is_object_address conjunct is always actually proven
// -- and only a conveyor-active conjunct's own establishing loop
// (oa_handle_precondition_stmt) ever writes into m_map at all; a
// symbolic-only one writes into the separate, weaker-trust m_symbolic_
// object_address_map instead (see oa_handle_precondition_stmt's own
// comment on why establishing into m_map from a symbolic fact would be
// unsound). Unrelated to what this test is actually about -- the
// symbolic-only CALL-SITE establishment mechanism just below.
struct conveyor_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr conveyor_ctrl conveyor_ctrl_v{};

struct handle {
  // Not itself conveyor-declared, so ACQUIRE's own 'this' carries no
  // automatic self-trust of any kind (D4324/P2680 soundness fix) --
  // needs its own explicit precondition before its own postcondition
  // can assert is_object_address(this) at all.
  void acquire ()
    pre<conveyor_ctrl_v>(std::is_object_address (this))
    post<symbolic_ctrl_v>(std::is_object_address (this)) {}
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
