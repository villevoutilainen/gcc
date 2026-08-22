// D4324 GIMPLE mirror of
// d4324-conveyor-call-relational-shift-contradiction.C: cg_call_
// relational_contradicts_p, the GIMPLE-native analogue of contracts.cc's
// own oa_call_relational_contradicts_p (see that AST-side test's own
// comment for the full reasoning). Wired into cg_check_call's own
// "param vs call" consult block, right alongside the existing "explicit
// fact implies REL_CODE" check that only ever proves TRUE.
//
// All four use an UNSIGNED idx (std::vector<int>::size_type), matching
// the AST-side sibling test's own identical setup, including its own
// companion ('v.size () > idx') and numeric-cap ('idx < 1000') facts --
// see that test's own comment for why both are independently needed
// (the subtraction itself, and the follow-on '+15'/'+4'/'+1' shift, can
// each wrap on their own) and why neither weakens what this test
// demonstrates. idx_signed_declines documents the real, fixed soundness
// bug this all traces back to (cg_match_shifted_comparison_against_call
// declines via oa_integral_conversion_value_preserving_p, exported from
// contracts.cc, when idx is signed).
//
// Every multi-fact function below nests its conditions as separate 'if'
// statements rather than joining them with '&&' -- see the AST-side
// sibling margin test's own comment for why: GIMPLE's own dominator-
// based fact propagation (cg_dom_fact_state.call_rel) does not currently
// carry a fact established in one conjunct of a single '&&' through to
// a later conjunct's own dominated block (a separate, pre-existing gap,
// unrelated to this fix; nested ifs are a mechanically equivalent way to
// write the same conditions that isn't affected by it).
//
// idx_signed_declines's own type-level decline was later found to be
// overly blunt -- see the AST-side sibling test's own comment for the
// full rationale (oa_convert_range_across_signedness, shared via cg_
// established_range_of here rather than duplicated).
// idx_signed_nonneg_violates demonstrates the rescue reaching all the
// way to the hard "provably violates" tier here too, not just plain
// consult: an added 'idx >= 0' conjunct (its own separate nested 'if',
// same reason as above) makes idx's own range -- queried at this exact
// program point, cg_established_range_of's own AT_STMT parameter --
// provably non-negative, so this verifies exactly like the unsigned
// shifted_past_the_boundary case above, including the error.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple -D_GLIBCXX_CONVEYOR_ASSERTIONS -D_GLIBCXX_PRECONDITION_ASSERTIONS" }

#include <vector>

int shifted_past_the_boundary (std::vector<int>& v,
				 std::vector<int>::size_type idx)
  pre<std::contracts::conveyor_assert_v>(std::is_object_address (&v))
{
  if (v.size () > idx)
    if (v.size () - idx < 5)
      if (idx < 1000)
	{
	  idx += 15;
	  return v[idx]; // { dg-error "provably violates the precondition" }
                             // { dg-message "is established \[^\n\]*, but the precondition requires" "established fact" { target *-*-* } .-1 }
	}
  return -1;
}

int exactly_at_the_edge (std::vector<int>& v,
			   std::vector<int>::size_type idx)
  pre<std::contracts::conveyor_assert_v>(std::is_object_address (&v))
{
  if (v.size () > idx)
    if (v.size () - idx < 5)
      if (idx < 1000)
	{
	  idx += 4;
	  return v[idx]; // { dg-error "provably violates the precondition" }
                             // { dg-message "is established \[^\n\]*, but the precondition requires" "established fact" { target *-*-* } .-1 }
	}
  return -1;
}

int genuinely_ambiguous (std::vector<int>& v,
			   std::vector<int>::size_type idx)
  pre<std::contracts::conveyor_assert_v>(std::is_object_address (&v))
{
  if (v.size () > idx)
    if (v.size () - idx < 5)
      if (idx < 1000)
	{
	  idx += 1;
	  return v[idx]; // { dg-warning {cannot verify that [^\n]* satisfies the precondition} }
	}
  return -1;
}

int idx_signed_declines (std::vector<int>& v, int idx)
  pre<std::contracts::conveyor_assert_v>(std::is_object_address (&v))
{
  if ((v.size () - idx) < 5)
    {
      idx += 15;
      // Uses [^\n]*, not .*, even though the text is byte-identical to
      // genuinely_ambiguous's own warning above: Tcl's regexp lets '.'
      // cross newlines by default, so a plain '.*' here would greedily
      // swallow all the way through to genuinely_ambiguous's own
      // message and leave this one unmatched.
      return v[idx]; // { dg-warning {cannot verify that [^\n]* satisfies the precondition} }
    }
  return -1;
}

int idx_signed_nonneg_violates (std::vector<int>& v, int idx)
  pre<std::contracts::conveyor_assert_v>(std::is_object_address (&v))
{
  if (idx >= 0)
    if (v.size () > idx)
      if (v.size () - idx < 5)
	if (idx < 1000)
	  {
	    idx += 15; // past the boundary, same as shifted_past_the_boundary
	    return v[idx]; // { dg-error "provably violates the precondition" }
	                   // { dg-message "is established \[^\n\]*, but the precondition requires" "established fact" { target *-*-* } .-1 }
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
