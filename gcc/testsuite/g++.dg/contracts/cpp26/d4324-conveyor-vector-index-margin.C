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
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -D_GLIBCXX_CONVEYOR_ASSERTIONS -D_GLIBCXX_PRECONDITION_ASSERTIONS" }

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
