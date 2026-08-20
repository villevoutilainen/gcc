// D4324 bounds-proving demo, GIMPLE mirror of d4324-conveyor-call-
// relational-range-vs-range-bug-parametric.C: the built-in GIMPLE pass's
// own range-vs-range fallback for a "param vs call" precondition
// (cg_check_call_range_relational, called from cg_predicate_facts_walk's
// own final pass, the only place STATE.call -- dominator-tracked, never
// flattened -- is safely available; see that function's own comment for
// why this specific shape can't live in cg_check_call directly the way
// the param-vs-param analogue does). Uses the same real, general
// 'resize (int n)' as the parametric AST test, exercising cg_collect_
// call_range_groups_parametric's own GIMPLE-side counterpart too.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }

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
  // -fcontract-conveyor-proofs-gimple and this precondition's own control
  // object, never by whether operator[] itself is conveyor-declared.
  int& operator[] (int idx) pre<conveyor_ctrl_v>(idx < size ())
  { return data[idx]; }

  void resize (int const m) post<conveyor_ctrl_v>(size () == m) { n = m; }
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
		// size () fact (unrelated to this change)
  return first + v[idx]; // { dg-error "provably violates the precondition" }
                          // { dg-message "is established \[^\n\]*, but the precondition requires" "established fact" { target *-*-* } .-1 }
}

int main ()
{
  test_vector v;
  return use_sound (v) + use_unsound (v);
}
