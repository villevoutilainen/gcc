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
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple -D_GLIBCXX_CONVEYOR_ASSERTIONS -D_GLIBCXX_PRECONDITION_ASSERTIONS" }

#include <vector>

int use_sound (std::vector<int>& v, int idx)
{
  if (v.size () - idx > 10)
    {
      idx += 5; // still within the established >10-element margin
      return v[idx]; // proven safe, no diagnostic
    }
  return -1;
}

int use_unsound (std::vector<int>& v, int idx)
{
  if (v.size () - idx > 10)
    {
      idx += 15; // past the established margin
      return v[idx]; // { dg-warning "cannot verify that .* satisfies the precondition" }
    }
  return -1;
}

int main ()
{
  std::vector<int> v(20);
  return use_sound (v, 0) + use_unsound (v, 0);
}
