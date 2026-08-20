// D4324 bounds-proving demo (see .claude/plans/lazy-stirring-pearl.md,
// Part 3): the parametric analogue of d4324-conveyor-call-relational-
// range-vs-range-bug.C -- 'resize' now takes a real argument, with
// 'post<>(size () == n)' relating the established call-range fact to
// another of resize's own parameters, not a fixed literal in the
// postcondition's own text. oa_call_range_conjunct_shape now accepts a
// bare PARM_DECL "other side" too; oa_collect_contract_call_ranges_
// parametric resolves it through this specific call site's own argument
// (here, the literal 5, giving the same exact-point range as the fixed-
// literal version) before folding it into the same call-range fact
// oa_env_check_call_relational_fact_1's own range-vs-range fallback
// (D4324's previous commit) already knows how to consult.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

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

struct test_vector {
  int data[8];
  int n = 0;

  int size () const conveyor { return n; }

  // Deliberately NOT conveyor -- the caller-obligation check is driven by
  // -fcontract-conveyor-proofs and this precondition's own control
  // object, never by whether operator[] itself is conveyor-declared.
  int& operator[] (int idx) pre<conveyor_ctrl_v>(idx < size ())
  { return data[idx]; }

  // A real, general resize -- 'n' (the parameter) is the postcondition's
  // own "other side", resolved through substitution at each call site.
  void resize (int const m) post<conveyor_ctrl_v>(size () == m) // { dg-warning "cannot verify postcondition" }
  { n = m; }
};

int use_sound (test_vector& v)
{
  v.resize (5);
  int idx = 3;
  return v[idx];
}

int use_unsound (test_vector& v)
{
  v.resize (5);
  int idx = 3;
  int first = v[idx];
  idx += 10;
  v.resize (5); // re-affirm: v[idx] above already invalidated v's own
		// size () fact (unrelated to this change -- see the
		// sibling, fixed-literal test's own comment)
  return first + v[idx]; // { dg-error "provably violates the precondition" }
                          // { dg-message "is established \[^\n\]*, but the precondition requires" "established fact" { target *-*-* } .-1 }
}

int main ()
{
  test_vector v;
  return use_sound (v) + use_unsound (v);
}
