// D4324 bounds-proving demo, GIMPLE mirror of
// d4324-conveyor-vector-index-margin.C: the built-in GIMPLE pass's own
// "RECEIVER.ACCESSOR () - PARAM OP <literal>" recognizer
// (cg_match_shifted_comparison_against_call, called from cg_refine_edge_
// into's own fallback when the top-level comparison's non-literal side
// isn't itself a bare call/field load) establishes the same margin fact
// as the AST side, via the existing cg_dom_fact_state.call_rel path.
//
// Getting this to fire at all against the real std::vector<int> (size ()
// returning size_t, unlike this pass's own earlier, int-returning test-
// only fixtures) surfaced a genuine, pre-existing gap in cg_cond_is_bare_
// param: comparing a signed int parameter against an unsigned accessor
// gimplifies with the parameter's own read materialized into a separate
// widening-cast temporary first, which that function never unwrapped --
// fixed alongside this shape's own recognizer, benefiting the existing
// bare "PARAM OP CALL ()" shape (cg_refine_relational_edge_into) too.
//
// use_definitely_unsound is a *third* tier, deliberately different from
// the margin-based cases below -- see that AST-side sibling test's own
// comment for why an outright, provable violation instead needs exact,
// independent ranges on both sides ('v.size () == 5' / 'idx = 100'),
// settled by the GIMPLE pass's own range-vs-range fact fallback.
//
// All the margin-based functions use an UNSIGNED idx (std::vector<int>::
// size_type), matching the AST-side sibling test's own identical setup.
// use_signed_idx_declines documents a real, fixed soundness bug shared
// with the AST engine -- cg_match_shifted_comparison_against_call
// declines to establish a margin fact from 'v.size () - idx' when idx is
// signed (the subtraction converts it to size_type first, so a negative
// idx would wrap; oa_integral_conversion_value_preserving_p, exported
// from contracts.cc, is consulted here too rather than duplicated).
//
// A SECOND, deeper soundness bug applies even to a genuinely unsigned
// idx: 'v.size () - idx' itself can wrap if idx > v.size (), regardless
// of idx's type -- fixed by requiring a companion, independently-sound
// direct fact ('v.size () > idx') before trusting the subtraction
// (cg_shifted_comparison_no_wrap_ok_p, consulting the same dominator-
// scoped state.call_rel map cg_refine_edge_into is about to populate).
// use_margin_only_declines demonstrates the margin alone, without that
// companion, no longer verifies even the plain access.
//
// A THIRD, related limit ON THE AST SIDE (see the AST-side sibling
// test's own use_shift_without_numeric_cap_declines): shifting an
// already-established margin fact by further arithmetic ('idx += 5') is
// its own separate composition, which can ALSO wrap even once the
// margin itself is trustworthy, needing an independently-provable
// NUMERIC bound on idx (cg_shift_arithmetic_no_wrap_ok_p, consulting
// idx's own established range via cg_established_range_of). GIMPLE,
// however, proves use_shift_without_numeric_cap_ok safe even WITHOUT an
// explicit numeric cap conjunct -- found via direct testing that
// cg_established_range_of's own gimple_ranger fallback (real SSA value-
// range propagation, not this file's own hand-rolled interval
// arithmetic) is independently strong enough to bound idx here on its
// own. This is a genuine, sound proof via a different, already-
// documented asymmetry ("GIMPLE is permanently stronger on range/
// dataflow mechanics" -- see project_gimple_ast_parity_direction), not
// a gap this fix needs to close; use_sound still adds an explicit
// numeric cap ('idx < 5') to demonstrate the mechanism this fix actually
// added, independent of whatever the ranger can also prove on its own.
//
// Every multi-fact function below nests its conditions as separate 'if'
// statements rather than joining them with '&&': found via direct
// testing that GIMPLE's own dominator-based fact propagation
// (cg_dom_fact_state.call_rel, populated per edge by cg_refine_edge_
// into) does not currently carry a fact established in one conjunct of
// a single '&&' condition through to a *later* conjunct's own dominated
// block -- confirmed with a minimal, completely unrelated repro ('if
// (idx < 100 && idx < v.size ()) return v[idx];' fails to verify even
// though the second conjunct alone is already sufficient). This is a
// separate, pre-existing GIMPLE-engine gap, not something this fix
// introduces or is required to close; nested ifs are a mechanically
// equivalent, unaffected way to write the same set of conditions.
//
// use_signed_idx_declines's own type-level decline was later found to be
// overly blunt -- see the AST-side sibling test's own comment for the
// full rationale (oa_convert_range_across_signedness, shared via
// cg_established_range_of here rather than duplicated). use_signed_idx_
// nonneg_ok demonstrates the rescue: an 'idx >= 0' conjunct makes idx's
// own range (queried at this exact program point -- cg_established_
// range_of's own AT_STMT parameter, without which a plain parameter's
// range is always the whole, unconstrained function-wide answer,
// regardless of any dominating check -- confirmed via direct testing)
// provably non-negative, so the conversion is exact and this verifies
// exactly like the unsigned case.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple -D_GLIBCXX_CONVEYOR_ASSERTIONS -D_GLIBCXX_PRECONDITION_ASSERTIONS" }

#include <vector>

int use_margin_only_declines (std::vector<int>& v, std::vector<int>::size_type idx)
{
  if (v.size () - idx > 10)
    {
      return v[idx]; // { dg-warning {cannot verify that [^\n]* satisfies the precondition} }
    }
  return -1;
}

int use_margin_with_companion_ok (std::vector<int>& v, std::vector<int>::size_type idx)
{
  if (v.size () > idx)
    if (v.size () - idx > 10)
      return v[idx]; // proven safe, no diagnostic
  return -1;
}

int use_shift_without_numeric_cap_ok (std::vector<int>& v,
					std::vector<int>::size_type idx)
{
  if (v.size () > idx)
    if (v.size () - idx > 10)
      {
	idx += 5;
	return v[idx]; // proven safe, no diagnostic -- see this file's own
			// comment on cg_established_range_of's gimple_ranger
      }
  return -1;
}

int use_sound (std::vector<int>& v, std::vector<int>::size_type idx)
{
  if (v.size () > idx)
    if (v.size () - idx > 10)
      if (idx < 5)
	{
	  idx += 5; // still within the established >10-element margin, and
		    // idx < 5 provably rules out overflow
	  return v[idx]; // proven safe, no diagnostic
	}
  return -1;
}

int use_unsound (std::vector<int>& v, std::vector<int>::size_type idx)
{
  if (v.size () > idx)
    if (v.size () - idx > 10)
      if (idx < 5)
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
      // Uses [^\n]*, not .*, even though the text is byte-identical to
      // the other warnings above: Tcl's regexp lets '.' cross newlines
      // by default, so a plain '.*' here would greedily swallow all the
      // way through to a later message and leave this one unmatched.
      return v[idx]; // { dg-warning {cannot verify that [^\n]* satisfies the precondition} }
    }
  return -1;
}

int use_signed_idx_nonneg_ok (std::vector<int>& v, int idx)
{
  if (idx >= 0)
    if (v.size () > idx)
      if (v.size () - idx > 10)
	return v[idx]; // proven safe, no diagnostic
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
	 + use_shift_without_numeric_cap_ok (v, 0)
	 + use_sound (v, 0) + use_unsound (v, 0)
	 + use_signed_idx_declines (v, 0) + use_signed_idx_nonneg_ok (v, 0)
	 + use_definitely_unsound (v);
}
