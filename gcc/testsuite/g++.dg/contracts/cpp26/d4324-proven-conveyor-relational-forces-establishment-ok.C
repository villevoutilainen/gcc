// D4324: proven_conveyor forces oa_establish_relational_from_call
// (item 6 for relational facts: a callee's own postcondition relating
// its return value to another parameter, e.g. 'post<ctrl>(r: r < q)',
// establishes a relational fact for the call's own LHS) on too, not
// just the call-obligation-discharge family. Mirrors
// d4324-conveyor-relational-postcondition-ok.C exactly, but with NO
// -fcontract-conveyor-proofs anywhere -- proven_conveyor alone must
// force both make_val's own precondition/postcondition checking and
// the relational-fact establishment consumer's own (also
// proven_conveyor, hence strict) precondition relies on.
//
// Before the fix to oa_establish_relational_from_call's own gate (it
// still checked flag_contract_conveyor_proofs/flag_contract_symbolic_
// proofs/oa_call_site_callback directly, with no forced-awareness),
// this compiled with a hard error ("cannot prove that 'y < q'"),
// because proven_conveyor is strict and nothing had established "y <
// q" for consumer's own precondition to consult.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

int make_val (int x, int const q)
  pre<sc::proven_conveyor_v> (x < q) post<sc::proven_conveyor_v> (r: r < q)
{ return x; }
int consumer (int y, int const q) pre<sc::proven_conveyor_v> (y < q)
{ return y; }

int caller (int x, int const q) pre<sc::proven_conveyor_v> (x < q)
{
  int y = make_val (x, q);
  return consumer (y, q);
}

int main () { return caller (2, 5) - 2; }
