// D4324: a symbolic-only precondition's own float FIELD conjunct
// (this->m_value >= 0.0) is now established and reaches a later
// postcondition's own self-check, with no intervening arithmetic --
// the missing float counterpart to the pre-existing integer field-range
// establishment path.  oa_establish_shared_substrate_self_trust already
// called oa_collect_contract_field_ranges/contract_field_range_set for
// an INTEGER field conjunct; the float counterpart
// (oa_collect_contract_float_field_ranges/contract_float_field_range_set)
// already existed too (used by oa_check_assertion_conjunct_against_env's
// own REAL_CST consult branch), but was simply never called from here --
// so a scalar-float-typed field precondition conjunct was silently never
// established at all for a symbolic-only precondition, even though the
// identical shape already worked for a conveyor-active one (via
// oa_handle_precondition_stmt's own separate classic self-trust block,
// which has no such int-only restriction).
//
// Note this is a narrower capability than it might look: this only
// covers a DIRECT, single-hop consult of the field's own established
// range against a literal (this->m_value >= 0.0, unchanged, right back
// out again) -- oa_get_range/oa_get_float_range's own COMPONENT_REF base
// case (used to feed a field's value into further arithmetic, e.g. a
// multiplication) still deliberately requires the fact to be
// conveyor-established, by design (a field fact can come from an
// unverified symbolic postcondition claim elsewhere, so the general-
// purpose composition path conservatively never trusts a symbolic-only
// one as an ingredient -- see that function's own comment). So a
// function whose body actually computes a NEW value from the field
// (this->m_value *= k;) still cannot have that postcondition proven
// under proven_symbolic, no matter how its precondition is written --
// see d4324-proven-symbolic-number-class-{ok,bad}.C for that boundary.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct F
{
  double m_value;

  void check ()
    pre<sc::proven_symbolic_v>(this->m_value >= 0.0)
    post<sc::proven_symbolic_v>(this->m_value >= 0.0)
  { }
};

int
main ()
{
  F f { 5.0 };
  f.check ();
  return 0;
}
