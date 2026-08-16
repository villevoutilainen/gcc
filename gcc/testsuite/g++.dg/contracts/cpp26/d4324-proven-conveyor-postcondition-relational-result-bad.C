// D4324: the direct repro of the postcondition proof gap this feature
// closes -- an ordinary relational claim about the postcondition's own
// named result ('post<ctrl>(r: r > 0)') was previously never checked
// against anything at all; oa_handle_postcondition_stmt built an
// almost-empty env (one is_object_address boolean only) and never
// called oa_check_assertion_conjunct_against_env. Here the claim is
// genuinely, provably false (the function always returns -1), so it
// must now be caught. No -fcontract-conveyor-proofs anywhere --
// proven_conveyor forces this on by itself.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

int
always_negative ()
  post<sc::proven_conveyor_v> (r: r > 0) // { dg-error "provably false" }
{
  return -1;
}

int main () { return always_negative () < 0 ? 0 : 1; }
