// D4324: the symbolic sibling of d4324-proven-conveyor-postcondition-
// relational-result-bad.C -- the identical, genuinely provably-false
// claim ('post<ctrl>(r: r > 0)' when the function always returns -1).
//
// D4324 correction: a symbolic postcondition's self-check is no longer
// blanket-exempt. This file used to be named ...-relational-exempt-ok.C
// and asserted the opposite: that this exact claim must compile clean
// under proven_symbolic even though the conveyor sibling catches it --
// that was the bug (the user's own diagnosis, found via the Number
// godbolt demo: a symbolic postcondition's non-symbolic-predicate
// conjuncts must be checked with the same rigor as conveyor's, not
// trusted outright merely for being symbolic). Now it correctly reports
// "provably false" too.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

int
always_negative ()
  post<sc::proven_symbolic_v> (r: r > 0) // { dg-error "provably false" }
{
  return -1;
}

int main () { return always_negative () < 0 ? 0 : 1; }
