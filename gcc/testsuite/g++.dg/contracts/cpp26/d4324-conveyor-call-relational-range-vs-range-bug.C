// D4324 bounds-proving demo (see .claude/plans/lazy-stirring-pearl.md):
// oa_env_check_call_relational_fact_1's own new range-vs-range fallback --
// a call-shaped precondition ("idx < size ()") can now be proven or
// disproven purely from each side's own *independently*-tracked fact
// (idx's own plain scalar range; v.size ()'s own established call-range
// fact, from an earlier call's own postcondition), with no if-condition or
// self-trust ever explicitly linking the two. Unlike every earlier
// call-relational test this session, the "bad" case here is a hard
// "provably violates" error, not just "cannot verify": the new fallback
// can reach OA_PROVEN_FALSE via disjoint ranges, not only OA_PROVEN_TRUE.
//
// 'operator[]' is deliberately NOT conveyor -- the caller-obligation check
// is driven by -fcontract-conveyor-proofs and this precondition's own
// control object, never by whether the callee itself is conveyor-declared.
//
// 'resize_to_5' deliberately takes no argument (a fixed literal in its own
// postcondition, 'size () == 5') rather than a general 'resize (int n)
// post<>(size () == n)' -- the latter needs oa_call_range_conjunct_shape
// to accept a PARM_DECL "other side", not just a literal, which is
// separate, not-yet-landed work (see the plan's own Part 3).
//
// use_unsound's own second access re-affirms size () via a second
// resize_to_5 () call immediately before it: the first v[idx] access
// already invalidated v's own established call-range fact for size ()
// (any call exposing v's address does, unconditionally, regardless of
// constness -- confirmed via oa_invalidate_symbolic_facts_for_call_args,
// contracts.cc, and accepted as correct, conservative behavior, not
// something to change).
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

  int& operator[] (int idx) pre<conveyor_ctrl_v>(idx < size ())
  { return data[idx]; }

  void resize_to_5 () post<conveyor_ctrl_v>(size () == 5) // { dg-warning "cannot verify postcondition" }
  { n = 5; }
};

int use_sound (test_vector& v)
{
  v.resize_to_5 ();
  int idx = 3;
  return v[idx];
}

int use_unsound (test_vector& v)
{
  v.resize_to_5 ();
  int idx = 3;
  int first = v[idx];
  idx += 10;
  v.resize_to_5 ();
  return first + v[idx]; // { dg-error "provably violates the precondition" }
                          // { dg-message "is established \[^\n\]*, but the precondition requires" "established fact" { target *-*-* } .-1 }
}

int main ()
{
  test_vector v;
  return use_sound (v) + use_unsound (v);
}
