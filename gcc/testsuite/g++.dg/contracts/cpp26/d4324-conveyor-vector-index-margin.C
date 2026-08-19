// D4324 bounds-proving demo (see .claude/plans/lazy-stirring-pearl.md,
// Part 4/5): the real, unmodified std::vector<int> instead of a test-only
// type -- operator[]'s own precondition comes from _GLIBCXX_PRECONDITION_
// ASSERTIONS (Stage P1), not a hand-written pre<>(). The if-condition
// establishes a margin via oa_match_shifted_comparison_against_call's own
// new "RECEIVER.ACCESSOR () - PARAM OP <literal>" shape: 'v.size () - idx
// > 10' means idx has strictly more than 10 elements of room before
// reaching size (), i.e. idx < size () - 10 (code LT_EXPR, offset -10 --
// see that function's own comment for the algebra). A subsequent shift
// within the margin (+5) still verifies; a shift past it (+15) no longer
// does, since the established fact and its own offset can no longer be
// shown to entail idx < size () -- showing the boundary exactly as
// requested.
//
// The first, unshifted access (not shown here -- see the sibling
// non-vector demos for that shape) needs no shift at all and is proven
// outright; this test's own point is specifically the shift-tracking
// interaction with a genuinely mandatory, real-library precondition.
//
// use_definitely_unsound is a *third* tier, deliberately different from
// the "past the margin" case above: that one only ever yields a one-
// sided lower bound on idx (a shifted-past margin can fail to be proven
// safe, but can't be disproven from a one-sided fact alone), so it can
// only ever reach "cannot verify," never "provably violates." Getting an
// outright, provable violation needs *exact*, independent ranges on both
// sides of the precondition: 'v.size () == 5' pins size ()'s own range to
// a single point (the pre-existing, non-Part-4 "CALL () OP LITERAL"
// shape), and 'idx = 100' pins idx's own plain range the same way; the
// range-vs-range fact fallback (oa_env_check_call_relational_fact_1, from
// this branch's own earlier "range-vs-range" work) then finds the two
// ranges provably disjoint and reports a hard error, not a warning.
//
// use_sound/use_unsound use an UNSIGNED idx (std::vector<int>::size_type)
// deliberately: 'v.size () - idx' performs the subtraction in size_type
// itself with no value-changing conversion of idx at all, so the
// established margin fact is genuinely sound. use_signed_idx_declines
// documents a real, fixed soundness bug: with a *signed* 'int idx',
// 'v.size () - idx' first converts idx to size_type via the usual
// arithmetic conversions (a negative idx wraps to a huge value), so a
// fact derived by naively stripping that conversion back to the bare
// signed idx would be unsound -- oa_match_shifted_comparison_against_
// call's own oa_integral_conversion_value_preserving_p guard now
// declines to establish it at all in this case, correctly falling back
// to "cannot verify" rather than wrongly proving v[idx] safe for a
// negative idx (see that guard's own comment for the concrete repro
// that motivated it).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -D_GLIBCXX_CONVEYOR_ASSERTIONS -D_GLIBCXX_PRECONDITION_ASSERTIONS" }

#include <vector>

int use_sound (std::vector<int>& v, std::vector<int>::size_type idx)
{
  if (v.size () - idx > 10)
    {
      idx += 5; // still within the established >10-element margin
      return v[idx]; // proven safe, no diagnostic
    }
  return -1;
}

int use_unsound (std::vector<int>& v, std::vector<int>::size_type idx)
{
  if (v.size () - idx > 10)
    {
      idx += 15; // past the established margin
      return v[idx]; // { dg-warning {cannot verify that [^\n]* satisfies the precondition} }
    }
  return -1;
}

int use_signed_idx_declines (std::vector<int>& v, int idx)
{
  if (v.size () - idx > 10)
    {
      // idx is signed here -- the subtraction above converts it to
      // size_type first, so no fact can be soundly attributed to the
      // raw signed idx (it could be negative). Correctly declines
      // rather than wrongly proving this safe. Regex uses [^\n]* (not
      // .*) and the '(...)idx' cast prefix distinguishing it from use_
      // unsound's own warning above: Tcl's regexp lets '.' cross
      // newlines by default, so two dg-warnings this close together
      // with a plain '.*' can have the first one's match greedily
      // swallow the second's text too.
      return v[idx]; // { dg-warning {cannot verify that [^\n]*\)idx[^\n]* satisfies the precondition} }
    }
  return -1;
}

int use_definitely_unsound (std::vector<int>& v)
{
  if (v.size () == 5)
    {
      int idx = 100; // nowhere close to size (), no margin involved at all
      return v[idx]; // { dg-error "provably violates the precondition" }
    }
  return -1;
}

int main ()
{
  std::vector<int> v(20);
  return use_sound (v, 0) + use_unsound (v, 0)
	 + use_signed_idx_declines (v, 0) + use_definitely_unsound (v);
}
