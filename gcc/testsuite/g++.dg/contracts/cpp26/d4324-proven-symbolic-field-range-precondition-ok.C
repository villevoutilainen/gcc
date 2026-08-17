// D4324: a symbolic function's own float field-range precondition is
// now enforced at call sites, matching the identical shape already
// enforced under proven_conveyor (see d4324-conveyor-proof-float-field-
// range-ok.C) and matching how a symbolic *scalar* precondition is
// already enforced at call sites too (oa_handle_precondition_simple_
// range_obligation). oa_handle_call_symbolic_precondition_obligation
// already had this consult for an INTEGER field-range conjunct; the
// floating-point counterpart (REAL_CST-bounded, e.g. 'this->m_value >=
// 0.0') was collected by neither loop, so the whole obligation silently
// went unchecked -- confirmed by direct testing before this fix (an
// object whose field was directly, provably negative produced no
// diagnostic at all calling check(), where the identical shape under
// proven_conveyor was already correctly caught).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct F
{
  double m_value;

  void check ()
    pre<sc::proven_symbolic_v>(this->m_value >= 0.0)
  { }
};

int
main ()
{
  F f;
  f.m_value = 5.0;
  f.check ();
  return 0;
}
