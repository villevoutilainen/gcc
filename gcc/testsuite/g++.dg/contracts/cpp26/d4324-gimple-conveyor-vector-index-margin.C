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
// the "past the margin" case above -- see that AST-side sibling test's
// own comment for why a one-sided shifted margin can only ever reach
// "cannot verify," never "provably violates," and why an outright,
// provable violation instead needs exact, independent ranges on both
// sides ('v.size () == 5' / 'idx = 100'), settled by the GIMPLE pass's
// own range-vs-range fact fallback.
//
// use_sound/use_unsound use an UNSIGNED idx (std::vector<int>::size_type)
// deliberately, matching the AST-side sibling test's own identical fix:
// use_signed_idx_declines documents a real, fixed soundness bug shared
// with the AST engine -- cg_match_shifted_comparison_against_call now
// declines to establish a margin fact from 'v.size () - idx' when idx is
// signed (the subtraction converts it to size_type first, so a negative
// idx would wrap; oa_integral_conversion_value_preserving_p, exported
// from contracts.cc, is consulted here too rather than duplicated). See
// that guard's own comment for the concrete repro that motivated it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple -D_GLIBCXX_CONVEYOR_ASSERTIONS -D_GLIBCXX_PRECONDITION_ASSERTIONS" }

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
      return v[idx]; // { dg-warning "cannot verify that [^\n]* satisfies the precondition" }
    }
  return -1;
}

int use_signed_idx_declines (std::vector<int>& v, int idx)
{
  if (v.size () - idx > 10)
    {
      // Regex includes the '(...)idx' cast prefix (present only for a
      // signed idx) to stay distinct from use_unsound's own identically-
      // worded warning above -- dejagnu's dg-warning matching gets
      // confused by two byte-identical regexes in the same file.
      return v[idx]; // { dg-warning "cannot verify that [^\n]*\\)idx[^\n]* satisfies the precondition" }
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
