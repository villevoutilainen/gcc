// D4324/P2680: a contract_assert's own condition naming a field through
// a plain, directly-named object ('n.m_value', not 'this->field'/'ptr->
// field') is genuinely checked against an already-established field-
// range fact, not silently left "cannot prove". oa_check_assertion_
// conjunct_against_env's own shape matchers previously covered a bare
// decl and a ptr->field access (oa_symbolic_comparison_conjunct_shape
// requires its base to be an INDIRECT_REF specifically), but never a
// directly-named object's own field -- confirmed missing by direct
// testing, closed by trying oa_match_general_comparison (already used
// by the call-obligation family) as a final fallback, checked directly
// via oa_env_check_float_range_subsumption/oa_env_check_range_
// subsumption (no substitution needed here, unlike at a call site: the
// conjunct's own decls already are in the current function's own
// terms).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct Number
{
  double m_value;
  explicit Number (double value)
    pre<sc::proven_conveyor_v>(value >= 0.0 && value <= 100.0)
    post<sc::proven_conveyor_v>(this->m_value >= 0.0 && this->m_value <= 100.0)
  { m_value = value; }
};

int main ()
{
  Number n (50.0);
  contract_assert<sc::proven_conveyor_v>(n.m_value >= 0.0 && n.m_value <= 100.0);
  return 0;
}
