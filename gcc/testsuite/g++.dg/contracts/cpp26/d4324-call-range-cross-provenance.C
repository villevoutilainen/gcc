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

// Re-establishes (without re-verifying) USE_IT's own symbolic call-range
// fact for 's', re-asserted mid-body below after CONSUME_CONVEYOR's call
// drops it -- same never_proven idiom __glibcxx_assert itself uses.
struct never_proven_symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  static constexpr bool never_proven (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr never_proven_symbolic_ctrl never_proven_symbolic_ctrl_v{};

struct S {
  int size () const conveyor { return 5; }
};

int consume_conveyor (const S& s) conveyor pre<conveyor_ctrl_v>(s.size () > 3) { return 0; }
int consume_symbolic (const S& s) pre<symbolic_ctrl_v>(s.size () > 3) { return 0; }

// USE_IT's own precondition establishes a *symbolic*-only call-range
// fact for 's' (self-trust), for the rest of its own body. The extra
// conveyor-flavored 'is_object_address(&s)' precondition is unrelated
// to what this test is about -- it's D4324/P2680's implicit reference-
// safety obligation on CONSUME_CONVEYOR's own 'const S&' parameter
// (item 7), which USE_IT, not being conveyor itself, has no other way
// to satisfy (unlike a conveyor caller, whose own reference parameters
// get this established implicitly). Both callees take 's' by CONST
// reference deliberately: a non-const 'S&' would additionally need 's'
// to be OWNED by USE_IT (P2680 9.1's cone-of-evaluation ownership rule,
// Q2) -- a merely-*proven-valid* borrowed parameter like USE_IT's own
// 's' can never satisfy that, no matter how it's proven, so a non-const
// signature here would make this call illegal regardless of the Q1
// fact this test is actually about.
int use_it (S& s) pre<conveyor_ctrl_v>(std::is_object_address (&s))
		   pre<symbolic_ctrl_v>(s.size () > 3)
{
  // CONSUME_CONVEYOR must run first: its own Q1 obligation on 's' is
  // satisfied by USE_IT's precondition-established fact above, but any
  // ordinary call taking 's' by reference (CONSUME_SYMBOLIC, right
  // below) conservatively invalidates that fact again afterward (same
  // "the callee might have done something to it" invalidation any
  // pointer/reference argument gets) -- nothing in this function could
  // re-derive it a second time, so the order here isn't arbitrary.
  int b = consume_conveyor (s); // { dg-warning "cannot verify that .int S::size\\(\\) const. called on .s." }
  // CONSUME_CONVEYOR's own call, just above, invalidated 's' call-range
  // fact too (the same conservative "the callee might have changed it"
  // rule, not specific to is_object_address) -- re-establish it,
  // symbolic-only, exactly as USE_IT's own precondition did originally,
  // so CONSUME_SYMBOLIC's consult below is still the intended "allowed
  // direction, silently proven" case this test is actually about.
  contract_assert<never_proven_symbolic_ctrl_v>(s.size () > 3);
  int a = consume_symbolic (s); // symbolic's own consult: allowed direction, silently proven
  return a + b;
}

int main ()
{
  S s;
  return use_it (s); // { dg-warning "cannot verify that .int S::size\\(\\) const." }
}
