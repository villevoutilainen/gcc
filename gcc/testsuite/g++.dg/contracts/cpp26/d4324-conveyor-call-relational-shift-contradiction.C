// D4324: oa_call_relational_contradicts_p -- complements oa_relational_
// code_implies (which only ever proves a call-relational fact entails
// the required relation, i.e. only ever proves TRUE). A shifted
// symbolic fact can also flatly *contradict* the required relation,
// with neither side ever pinned to an absolute number: 'v.size () - idx
// < 5' establishes 'idx > v.size () - 5' (a GT_EXPR fact, offset -5);
// shifting idx by +15 gives 'idx' > v.size () + 10' (offset +10), which
// can never be < v.size () -- provably violates the precondition,
// regardless of what v.size () actually is. Before this, only an
// explicit numeric range-vs-range fallback (both sides pinned to
// literals, see d4324-conveyor-vector-index-margin.C's own
// use_definitely_unsound) could ever reach "provably violates" for a
// call-relational fact; a shifted-but-still-symbolic fact could only
// ever get as far as "cannot verify," even when arbitrarily far past
// the boundary (found via direct testing: an established lower bound
// shifted by a huge, unambiguous amount still produced no error at all
// before this fix).
//
// exactly_at_the_edge is the interesting boundary case: idx += 4 makes
// the established fact 'idx' > v.size () - 1', i.e. idx' >= v.size ()
// exactly -- still, correctly, a provable violation (operator[]'s own
// precondition is the *strict* 'idx < size ()'), confirming no off-by-
// one slop in the inclusive-bound conversion this reuses from
// oa_tighten_range_bound. genuinely_ambiguous (+1 instead) still only
// reaches "cannot verify," confirming the fix doesn't overreach into
// cases that remain genuinely undecidable.
//
// All four use an UNSIGNED idx (std::vector<int>::size_type), and (see
// d4324-conveyor-vector-index-margin.C's own comment for the full
// rationale) need TWO additional, independently-sound facts before any
// of this is trustworthy: a companion direct comparison ('v.size () >
// idx', ruling out the subtraction itself wrapping) and a numeric cap
// ('idx < 1000', ruling out the FOLLOW-ON shift -- '+15'/'+4'/'+1' --
// itself overflowing size_type). Neither weakens what this test
// demonstrates: both facts are independently, separately sound (see
// oa_shifted_comparison_no_wrap_ok_p/oa_shift_arithmetic_no_wrap_ok_p's
// own comments), and the contradiction/ambiguity being tested is exactly
// as before, just no longer built on an unsound foundation.
// idx_signed_declines documents a real, fixed soundness bug: for a
// *signed* 'int idx', the same source shape converts idx to size_type
// before subtracting (a negative idx wraps), so oa_match_shifted_
// comparison_against_call's own oa_integral_conversion_value_preserving_p
// guard declines to establish any fact at all rather than risk an
// unsound "provably violates" conclusion.
//
// idx_signed_declines's own type-level decline was later found to be
// overly blunt -- see d4324-conveyor-vector-index-margin.C's own comment
// for the full rationale (oa_convert_range_across_signedness).
// idx_signed_nonneg_violates demonstrates the rescue reaching all the
// way to the hard "provably violates" tier, not just plain consult: an
// added 'idx >= 0' conjunct makes idx's own range provably non-negative,
// so the conversion is exact and this verifies exactly like the
// unsigned shifted_past_the_boundary case above, including the error.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -D_GLIBCXX_CONVEYOR_ASSERTIONS -D_GLIBCXX_PRECONDITION_ASSERTIONS" }

#include <vector>

int shifted_past_the_boundary (std::vector<int>& v,
				 std::vector<int>::size_type idx)
{
  if (v.size () > idx && (v.size () - idx) < 5 && idx < 1000)
    {
      idx += 15;
      return v[idx]; // { dg-error "provably violates the precondition" }
    }
  return -1;
}

int exactly_at_the_edge (std::vector<int>& v,
			   std::vector<int>::size_type idx)
{
  if (v.size () > idx && (v.size () - idx) < 5 && idx < 1000)
    {
      idx += 4;
      return v[idx]; // { dg-error "provably violates the precondition" }
    }
  return -1;
}

int genuinely_ambiguous (std::vector<int>& v,
			   std::vector<int>::size_type idx)
{
  if (v.size () > idx && (v.size () - idx) < 5 && idx < 1000)
    {
      idx += 1;
      return v[idx]; // { dg-warning {cannot verify that [^\n]* satisfies the precondition} }
    }
  return -1;
}

int idx_signed_declines (std::vector<int>& v, int idx)
{
  if ((v.size () - idx) < 5)
    {
      idx += 15; // would be past the boundary if idx were unsigned
      // Regex uses [^\n]* (not .*) and the '(...)idx' cast prefix
      // distinguishing it from genuinely_ambiguous's own warning above:
      // Tcl's regexp lets '.' cross newlines by default, so two dg-
      // warnings this close together with a plain '.*' can have the
      // first one's match greedily swallow the second's text too.
      return v[idx]; // { dg-warning {cannot verify that [^\n]*\)idx[^\n]* satisfies the precondition} }
    }
  return -1;
}

int idx_signed_nonneg_violates (std::vector<int>& v, int idx)
{
  if (idx >= 0 && v.size () > idx && (v.size () - idx) < 5 && idx < 1000)
    {
      idx += 15; // past the boundary, same as shifted_past_the_boundary
      return v[idx]; // { dg-error "provably violates the precondition" }
    }
  return -1;
}

int main ()
{
  std::vector<int> v (20);
  return shifted_past_the_boundary (v, 0)
	 + exactly_at_the_edge (v, 0)
	 + genuinely_ambiguous (v, 0)
	 + idx_signed_declines (v, 0)
	 + idx_signed_nonneg_violates (v, 0);
}
