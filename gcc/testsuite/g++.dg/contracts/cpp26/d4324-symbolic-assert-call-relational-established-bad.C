// D4324: a call-relational fact established only as symbolic (i.e. by
// a symbolic-only precondition, conveyor_established == false) must
// still be usable by a symbolic-only contract_assert's own consult
// (require_conveyor == false there too) -- oa_env_check_call_relational_
// fact_1's own "(!require_conveyor || fact.conveyor_established)" gate
// must accept this, not just a conveyor-established fact. Distinct from
// d4324-symbolic-assert-call-relational-accessor-bad.C, which instead
// exercises the *accessor's own tag* (symbolic vs conveyor) -- here the
// accessor is an ordinary conveyor-tagged (real, defined) one, and only
// the *establishing precondition's own flavor* is symbolic-only.
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

struct conveyor_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr conveyor_ctrl conveyor_ctrl_v{};

struct S {
  int size () const conveyor { return 5; }
};

// A symbolic-only precondition (not conveyor-active) naming a real,
// conveyor-tagged accessor: the fact it establishes is tagged
// conveyor_established == false, since the *establishing* control
// object here is symbolic-only.
int
f (int x, S& v)
  pre<conveyor_ctrl_v>(std::is_object_address (&v))
  pre<symbolic_ctrl_v>(x < v.size ())
{
  contract_assert<symbolic_ctrl_v>(x > v.size ()); // { dg-error "condition .*size.*is provably false" }
                                                    // { dg-message "established \[^\n\]*" "established fact" { target *-*-* } .-1 }
  return 0;
}

int
main ()
{
  S v;
  return f (1, v); // { dg-warning "cannot verify that .1. satisfies the precondition" }
}
