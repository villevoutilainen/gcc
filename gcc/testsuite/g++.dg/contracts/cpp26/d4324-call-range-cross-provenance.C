// D4324: m_contract_call_range_map is a shared substrate between
// -fcontract-conveyor-proofs and -fcontract-symbolic-proofs, exactly
// like the pre-existing ptr->field range map -- but the trust direction
// is one-way. A call-range fact established only via a *symbolic*-
// flavored contract satisfies a later *symbolic* obligation (the
// allowed direction) but must NOT satisfy a *conveyor* obligation
// (conveyor's own consult requires conveyor_established specifically):
// "the conveyor analysis cannot trust a symbolic predicate."
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -fcontract-symbolic-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct conveyor_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr conveyor_ctrl conveyor_ctrl_v{};

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

struct S {
  int size () const conveyor { return 5; }
};

int consume_conveyor (S& s) conveyor pre<conveyor_ctrl_v>(s.size () > 3) { return 0; }
int consume_symbolic (S& s) pre<symbolic_ctrl_v>(s.size () > 3) { return 0; }

// USE_IT's own precondition establishes a *symbolic*-only call-range
// fact for 's' (self-trust), for the rest of its own body.
int use_it (S& s) pre<symbolic_ctrl_v>(s.size () > 3)
{
  int a = consume_symbolic (s); // symbolic's own consult: allowed direction, silently proven
  int b = consume_conveyor (s); // { dg-warning "cannot verify that .int S::size\\(\\) const. called on .s." }
  return a + b;
}

int main ()
{
  S s;
  return use_it (s); // { dg-warning "cannot verify that .int S::size\\(\\) const." }
}
