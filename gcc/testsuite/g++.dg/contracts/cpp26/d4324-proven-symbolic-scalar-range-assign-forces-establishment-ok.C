// D4324: proven_symbolic forces m_contract_scalar_range_map's own
// establishment site (INIT_EXPR/MODIFY_EXPR's "static-prover analogue
// of the runtime shadow tracking", the Mechanism B-adjacent scalar
// range map -- distinct from the always-on, mandatory m_range_map)
// on too, when a plain assignment (not a declaration's own
// initializer) reassigns a bare int from a call whose postcondition
// establishes a range. No proofs flag anywhere; proven_symbolic alone
// must force it.
//
// Found to be a real, live gap (not assumed) by direct testing: before
// this fix, this exact program compiled cleanly with
// -fcontract-symbolic-proofs added, and failed with "cannot prove that
// 'n' satisfies the precondition of 'int consumer(int)'" without it,
// because this establishment site was still gated on flag_contract_
// symbolic_proofs/flag_contract_conveyor_proofs/oa_call_site_callback
// alone. (The DECL_EXPR shape 'int n = make_count ();' does not
// exercise this same site -- a declaration's own initializer never
// reaches m_contract_scalar_range_map at all, proving instead through
// the always-on nonzero/mandatory-range tracking, which cannot supply
// the tighter '[1,100)' bound consumer's precondition needs here.)
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

int make_count () post<sc::proven_symbolic_v>(r: r >= 1 && r < 100) { return 5; }
int consumer (int n) pre<sc::proven_symbolic_v>(n >= 1 && n < 100) { return n; }

int g ()
{
  int n;
  n = make_count ();
  return consumer (n);
}

int main () { return g () - 5; }
