// D4324 bounds-proving demo (see .claude/plans/lazy-stirring-pearl.md,
// Part 4/5): the real, unmodified std::vector<int> instead of a test-only
// type -- operator[]'s own precondition comes from _GLIBCXX_PRECONDITION_
// ASSERTIONS (Stage P1), not a hand-written pre<>(). The if-condition
// establishes a margin via oa_match_shifted_comparison_against_call's own
// "RECEIVER.ACCESSOR () - PARAM OP <literal>" shape: 'v.size () - idx >
// 10' means idx has strictly more than 10 elements of room before
// reaching size (), i.e. idx < size () - 10 (code LT_EXPR, offset -10 --
// see that function's own comment for the algebra).
//
// use_definitely_unsound is a *third* tier, deliberately different from
// the margin-based cases below: an outright, provable violation needs
// *exact*, independent ranges on both sides of the precondition:
// 'v.size () == 5' pins size ()'s own range to a single point (the pre-
// existing, non-Part-4 "CALL () OP LITERAL" shape), and 'idx = 100' pins
// idx's own plain range the same way; the range-vs-range fact fallback
// (oa_env_check_call_relational_fact_1, from this branch's own earlier
// "range-vs-range" work) then finds the two ranges provably disjoint and
// reports a hard error, not a warning.
//
// All the margin-based functions use an UNSIGNED idx (std::vector<int>::
// size_type) deliberately: 'v.size () - idx' performs the subtraction in
// size_type itself with no value-changing conversion of idx at all.
// use_signed_idx_declines documents a real, fixed soundness bug: with a
// *signed* 'int idx', 'v.size () - idx' first converts idx to size_type
// via the usual arithmetic conversions (a negative idx wraps to a huge
// value), so a fact derived by naively stripping that conversion back to
// the bare signed idx would be unsound -- oa_match_shifted_comparison_
// against_call's own oa_integral_conversion_value_preserving_p guard
// declines to establish it at all in this case.
//
// A SECOND, deeper soundness bug applies even to a genuinely unsigned
// idx: 'v.size () - idx' itself can wrap (regardless of idx's type) if
// idx > v.size (), since the wrapped difference is astronomically larger
// than any realistic literal threshold -- the single observed conjunct
// carries no information ruling that out. Fixed by requiring a
// companion, independently-sound direct fact ('v.size () > idx',
// established via oa_match_comparison_against_call, which is safe on its
// own: a wrapped idx would make it false, not true) before trusting the
// subtraction (oa_shifted_comparison_no_wrap_ok_p). use_margin_only_
// declines demonstrates the margin ALONE, without that companion, no
// longer verifies even the plain (unshifted) access.
//
// A THIRD, related limit: shifting an already-established margin fact by
// further arithmetic ('idx += 5') is it own separate composition
// (oa_get_call_relational's own PLUS_EXPR handling), which can ALSO wrap
// even once the margin itself is trustworthy -- idx could still be
// arbitrarily close to size_type's own max. This also needs an
// independently-provable NUMERIC bound on idx (not merely a symbolic
// relation to size ()) tight enough that the shift provably can't
// overflow (oa_shift_arithmetic_no_wrap_ok_p, consulting idx's own
// established range via oa_get_range). use_shift_without_numeric_cap_
// declines shows the margin+companion alone still isn't enough once a
// further shift is involved; use_sound adds the missing numeric cap
// ('idx < 5') and the shift verifies.
//
// use_signed_idx_declines's own type-level decline (the FIRST bug above)
// was later found to be overly blunt: it refuses ANY signed idx, even
// when the analysis already knows enough to rule out the unsound case.
// oa_get_range gained real signed-to-unsigned conversion semantics
// (oa_convert_range_across_signedness): a NOP_EXPR/CONVERT_EXPR that
// isn't value-preserving by type alone is no longer blindly stripped --
// its own inner (pre-conversion) range is computed and mapped through
// the actual conversion, exactly when that range falls entirely on one
// side of zero (entirely non-negative: passes through unchanged, exact;
// entirely negative: shifts up by 2^M, exact). A range that straddles
// zero still correctly declines -- confirmed together with the user via
// a concrete counterexample that no single interval can soundly cover
// both halves (the negative half's own converted value lands near the
// type's opposite end, not anywhere near the non-negative half).
// use_signed_idx_nonneg_ok demonstrates the rescue: same shape as use_
// signed_idx_declines, with an added 'idx >= 0' conjunct establishing
// idx's own range as entirely non-negative, verifies exactly like the
// unsigned case now.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -D_GLIBCXX_CONVEYOR_ASSERTIONS -D_GLIBCXX_PRECONDITION_ASSERTIONS" }

#include <vector>

int use_margin_only_declines (std::vector<int>& v, std::vector<int>::size_type idx)
{
  if (v.size () - idx > 10)
    {
      // No companion 'v.size () > idx' fact -- the subtraction itself
      // could have wrapped (idx > v.size ()), so even this plain,
      // unshifted access can no longer be proven safe.
      return v[idx]; // { dg-warning {cannot verify that [^\n]* satisfies the precondition} }
    }
  return -1;
}

int use_margin_with_companion_ok (std::vector<int>& v, std::vector<int>::size_type idx)
{
  if (v.size () > idx && v.size () - idx > 10)
    {
      return v[idx]; // proven safe, no diagnostic
    }
  return -1;
}

int use_shift_without_numeric_cap_declines (std::vector<int>& v,
					      std::vector<int>::size_type idx)
{
  if (v.size () > idx && v.size () - idx > 10)
    {
      idx += 5; // no numeric cap on idx -- could still be near size_type's
		// own max, so this shift can't be proven not to overflow
      return v[idx]; // { dg-warning {cannot verify that [^\n]* satisfies the precondition} }
    }
  return -1;
}

int use_sound (std::vector<int>& v, std::vector<int>::size_type idx)
{
  if (v.size () > idx && v.size () - idx > 10 && idx < 5)
    {
      idx += 5; // idx < 5, so idx+5 < 10 -- provably far from overflow,
		// and still within the established >10-element margin
      return v[idx]; // proven safe, no diagnostic
    }
  return -1;
}

int use_unsound (std::vector<int>& v, std::vector<int>::size_type idx)
{
  if (v.size () > idx && v.size () - idx > 10 && idx < 5)
    {
      idx += 15; // past the established margin (idx+15 could be as large
		 // as size ()+4)
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
      // .*) and the '(...)idx' cast prefix distinguishing it from the
      // other warnings above: Tcl's regexp lets '.' cross newlines by
      // default, so two dg-warnings this close together with a plain
      // '.*' can have the first one's match greedily swallow the
      // second's text too.
      return v[idx]; // { dg-warning {cannot verify that [^\n]*\)idx[^\n]* satisfies the precondition} }
    }
  return -1;
}

int use_signed_idx_nonneg_ok (std::vector<int>& v, int idx)
{
  if (idx >= 0 && v.size () > idx && v.size () - idx > 10)
    {
      // idx is signed, but its own range (established by 'idx >= 0') is
      // now provably entirely non-negative, so the conversion to
      // size_type is exact for its actual value -- verifies exactly
      // like the unsigned case.
      return v[idx]; // proven safe, no diagnostic
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
  return use_margin_only_declines (v, 0) + use_margin_with_companion_ok (v, 0)
	 + use_shift_without_numeric_cap_declines (v, 0)
	 + use_sound (v, 0) + use_unsound (v, 0)
	 + use_signed_idx_declines (v, 0) + use_signed_idx_nonneg_ok (v, 0)
	 + use_definitely_unsound (v);
}
