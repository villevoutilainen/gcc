// D4324: a proven_symbolic postcondition's own established range fact
// must reach a later contract_assert, exactly like a proven_conveyor
// postcondition already does -- previously it didn't: oa_call_
// postcondition_range_p (oa_get_range's own item-6 fallback, feeding
// the untagged m_range_map) was hardcoded to only consider conveyor-
// active postconditions, so a purely symbolic-active postcondition
// left the declared decl with no range fact at all. Found via a
// user-supplied repro (https://godbolt.org/z/Enqq34dKe). Fixed by
// gating on oa_contract_fact_tracking_active_p (either flavor) instead,
// matching the file's own already-correct oa_call_symbolic_predicate_p.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

int
val () post<sc::proven_symbolic_v>(r: r == 666)
{
  return 666;
}

int
main ()
{
  int x = val ();
  if (x < 667)
    contract_assert<sc::proven_symbolic_v>(x < 667);
  contract_assert<sc::proven_symbolic_v>(x < 667);
  return 0;
}
