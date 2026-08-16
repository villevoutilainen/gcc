// D4324: a proven_symbolic precondition's own bare 'param OP literal'
// combined-range obligation, discharged against a literal call-site
// argument -- previously oa_handle_call_symbolic_precondition_
// obligation had no mechanism at all for this shape (only field
// comparisons, param-vs-param, and predicate calls), for either int or
// float: every pre-existing proven_symbolic test happened to avoid it.
// Fixed via a new function, oa_handle_precondition_simple_range_
// obligation, shared with the conveyor flavor's own identical
// mechanism (oa_handle_call_conveyor_proof_obligation).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

void
take_percentage (int p) pre<sc::proven_symbolic_v>(p >= 0 && p <= 100)
{
  (void) p;
}

void
good_call ()
{
  take_percentage (50);
}

int main () { good_call (); return 0; }
