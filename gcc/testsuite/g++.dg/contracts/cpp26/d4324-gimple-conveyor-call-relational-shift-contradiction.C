// D4324 GIMPLE mirror of
// d4324-conveyor-call-relational-shift-contradiction.C: cg_call_
// relational_contradicts_p, the GIMPLE-native analogue of contracts.cc's
// own oa_call_relational_contradicts_p (see that AST-side test's own
// comment for the full reasoning). Wired into cg_check_call's own
// "param vs call" consult block, right alongside the existing "explicit
// fact implies REL_CODE" check that only ever proves TRUE.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple -D_GLIBCXX_CONVEYOR_ASSERTIONS -D_GLIBCXX_PRECONDITION_ASSERTIONS" }

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
