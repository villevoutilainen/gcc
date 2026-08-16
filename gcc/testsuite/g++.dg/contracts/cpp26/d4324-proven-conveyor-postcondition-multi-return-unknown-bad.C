// D4324: the core new capability -- a postcondition's own condition is
// now checked against a full oa_env merged across *every* RETURN_EXPR
// (not just an artifact of walk order), the same assign()-then-
// merge_with-family idiom TRY_BLOCK/SWITCH_STMT already use for their
// own N-arm joins. Two return paths: one where 'r > 0' genuinely holds
// (returns 1) and one where it doesn't (returns -1) -- the merged claim
// must be rejected as unprovable (proven_conveyor is strict, so
// "cannot prove", not silently "proven" from only whichever return the
// old, walk-order-dependent env happened to reflect last).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

int
maybe_negative (bool cond)
  post<sc::proven_conveyor_v> (r: r > 0) // { dg-error "cannot prove" }
{
  if (cond)
    return 1;
  return -1;
}

int main () { return maybe_negative (true); }
