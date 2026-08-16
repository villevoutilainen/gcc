// D4324: oa_check_assertion_conjunct_against_env's call-relational
// shape must recognize a DECL_DECLARED_SYMBOLIC_P accessor (declared,
// never defined -- an axiom, not a real function), not just a
// DECL_DECLARED_CONVEYOR_P one -- see d4324-symbolic-call-relational-
// symbolic-accessor.C's own identical widening for the call-obligation
// engine. f's own precondition establishes 'x < s.size ()'; the
// contract_assert's own claim 'x > s.size ()' flatly contradicts it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct symbolic_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

struct Sym {
  int size () const symbolic; // declared, never defined: an axiom
};

int
f (int x, Sym& s) pre<symbolic_ctrl_v>(x < s.size ())
{
  contract_assert<symbolic_ctrl_v>(x > s.size ()); // { dg-error "condition .*size.*is provably false" }
  return 0;
}

int
main ()
{
  Sym s;
  return f (1, s); // { dg-warning "cannot verify that .1. satisfies the precondition" }
}
