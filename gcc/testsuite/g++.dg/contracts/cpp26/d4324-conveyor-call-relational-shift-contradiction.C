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
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -D_GLIBCXX_CONVEYOR_ASSERTIONS -D_GLIBCXX_PRECONDITION_ASSERTIONS" }

#include <vector>

int shifted_past_the_boundary (std::vector<int>& v, int idx)
{
  if ((v.size () - idx) < 5)
    {
      idx += 15;
      return v[idx]; // { dg-error "provably violates the precondition" }
    }
  return -1;
}

int exactly_at_the_edge (std::vector<int>& v, int idx)
{
  if ((v.size () - idx) < 5)
    {
      idx += 4;
      return v[idx]; // { dg-error "provably violates the precondition" }
    }
  return -1;
}

int genuinely_ambiguous (std::vector<int>& v, int idx)
{
  if ((v.size () - idx) < 5)
    {
      idx += 1;
      return v[idx]; // { dg-warning "cannot verify that .* satisfies the precondition" }
    }
  return -1;
}

int main ()
{
  std::vector<int> v (20);
  return shifted_past_the_boundary (v, 0)
	 + exactly_at_the_edge (v, 0)
	 + genuinely_ambiguous (v, 0);
}
