// D4324 GIMPLE mirror of
// d4324-conveyor-call-relational-shift-contradiction.C: cg_call_
// relational_contradicts_p, the GIMPLE-native analogue of contracts.cc's
// own oa_call_relational_contradicts_p (see that AST-side test's own
// comment for the full reasoning). Wired into cg_check_call's own
// "param vs call" consult block, right alongside the existing "explicit
// fact implies REL_CODE" check that only ever proves TRUE.
//
// All three use an UNSIGNED idx (std::vector<int>::size_type), matching
// the AST-side sibling test's own identical fix -- see
// idx_signed_declines below and that test's own comment for the real,
// fixed soundness bug this documents (cg_match_shifted_comparison_
// against_call now declines via oa_integral_conversion_value_preserving_p,
// exported from contracts.cc, when idx is signed).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple -D_GLIBCXX_CONVEYOR_ASSERTIONS -D_GLIBCXX_PRECONDITION_ASSERTIONS" }

#include <vector>

int shifted_past_the_boundary (std::vector<int>& v,
				 std::vector<int>::size_type idx)
{
  if ((v.size () - idx) < 5)
    {
      idx += 15;
      return v[idx]; // { dg-error "provably violates the precondition" }
    }
  return -1;
}

int exactly_at_the_edge (std::vector<int>& v,
			   std::vector<int>::size_type idx)
{
  if ((v.size () - idx) < 5)
    {
      idx += 4;
      return v[idx]; // { dg-error "provably violates the precondition" }
    }
  return -1;
}

int genuinely_ambiguous (std::vector<int>& v,
			   std::vector<int>::size_type idx)
{
  if ((v.size () - idx) < 5)
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

int main ()
{
  std::vector<int> v (20);
  return shifted_past_the_boundary (v, 0)
	 + exactly_at_the_edge (v, 0)
	 + genuinely_ambiguous (v, 0)
	 + idx_signed_declines (v, 0);
}
