// D4324: proven_symbolic forces the DECL_EXPR return-value predicate
// establishment site on too, not just the call-obligation-discharge
// family. Mirrors d4324-symbolic-proof-predicate-returnvalue-ok.C
// (which needs -fcontract-symbolic-proofs) exactly, except here NO
// proofs flag is given at all -- proven_symbolic alone must force both
// 'int r = produce ();' establishing check_it (r), and consume's own
// separately-forced precondition consulting it.
//
// Found to be a real, live gap (not assumed) by direct testing: before
// this fix, this exact program compiled cleanly under the *previous*
// commit and failed with "cannot prove that 'bool check_it(int)' ('r')
// holds" here, because the DECL_EXPR establishment in oa_walk_stmt was
// still gated on flag_contract_symbolic_proofs/flag_contract_conveyor_
// proofs/oa_call_site_callback alone, with no forced-awareness -- even
// though consume's own precondition check was already correctly forced
// by proven_symbolic itself.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

bool check_it (int) symbolic;

int produce () post<sc::proven_symbolic_v>(r: check_it (r)) { return 1; }
void consume (int x) pre<sc::proven_symbolic_v>(check_it (x)) { (void) x; }

int main ()
{
  int r = produce ();
  consume (r);
  return 0;
}
