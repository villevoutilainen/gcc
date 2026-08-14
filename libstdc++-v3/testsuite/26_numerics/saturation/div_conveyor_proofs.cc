// D4324: call-site static proof of saturating_div's own declared
// _GLIBCXX_PRECONDITION_NONZERO_DIVISOR precondition under
// -fcontract-conveyor-proofs (backed by gcc/cp/contracts.cc's own
// oa_nonzero_conjunct_p-based obligation check, added specifically so
// this declared precondition -- unlike the plain body-internal assert
// it replaces -- is actually enforced at other conveyor callers' own
// call sites, not just within saturating_div's own body). A literal
// zero divisor is rejected outright; an unconstrained divisor is left
// as an unresolved "cannot verify" obligation; a properly guarded
// divisor is silently proven safe.
// { dg-options "-D_GLIBCXX_CONVEYOR_ASSERTIONS -D_GLIBCXX_PRECONDITION_ASSERTIONS -fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }
// { dg-do compile { target c++26 } }

#include <numeric>

int literal_zero_bad(int x) conveyor
{
  return std::saturating_div(x, 0); // { dg-error "provably violates the precondition" }
}

int unconstrained_unknown(int x, int y) conveyor
{
  return std::saturating_div(x, y); // { dg-warning "cannot verify that .y. is nonzero" }
}

int guarded_ok(int x, int y) conveyor
{
  if (y != 0)
    return std::saturating_div(x, y);
  return 0;
}
