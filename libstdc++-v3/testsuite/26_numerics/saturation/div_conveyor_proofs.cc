// D4324: call-site static proof of saturating_div's own declared
// _GLIBCXX_PRECONDITION_NONZERO_DIVISOR precondition under
// -fcontract-conveyor-proofs (backed by gcc/cp/contracts.cc's own
// oa_nonzero_conjunct_p-based obligation check, added specifically so
// this declared precondition -- unlike the plain body-internal assert
// it replaces -- is actually enforced at other conveyor callers' own
// call sites, not just within saturating_div's own body). A literal
// zero divisor is rejected outright; so is an unconstrained one, since a
// conveyor caller must establish a conveyor callee's preconditions (a
// conveyor rule, not just a proof); a properly guarded divisor is
// silently proven safe.  The same holds for a call in a strict
// predicate, whose earlier conjuncts can establish the divisor.
// { dg-options "-D_GLIBCXX_CONVEYOR_ASSERTIONS -D_GLIBCXX_PRECONDITION_ASSERTIONS -fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }
// { dg-do compile { target c++26 } }

#include <contracts>
#include <numeric>

int literal_zero_bad(int x) conveyor
{
  return std::saturating_div(x, 0); // { dg-error "provably violates the precondition" }
}

int unconstrained_unknown(int x, int y) conveyor
{
  return std::saturating_div(x, y); // { dg-error "cannot prove that .y. is nonzero" }
}

int guarded_ok(int x, int y) conveyor
{
  if (y != 0)
    return std::saturating_div(x, y);
  return 0;
}

void pred_unconstrained(int x, int y)
  pre<std::contracts::conveyor_assert_v>(std::saturating_div(x, y) > 0) // { dg-error "cannot prove that .y. is nonzero" }
{ }

void pred_guarded(int x, int y)
  pre<std::contracts::conveyor_assert_v>(y != 0
					 && std::saturating_div(x, y) > 0)
{ }
