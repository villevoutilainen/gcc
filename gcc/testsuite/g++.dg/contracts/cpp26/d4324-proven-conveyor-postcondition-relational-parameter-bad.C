// D4324: the postcondition proof gap's own parameter-relational
// variant -- a postcondition can also claim something about an
// ordinary parameter, not just its own named result (parameters
// referenced in a postcondition must be const, so this doesn't go via
// reassignment: instead the claim directly contradicts the function's
// own precondition, which is unconditionally self-trusted and so
// reaches the merged env every return sees). 'x < 0' is established as
// a fact at function entry by the precondition; the postcondition's
// own 'x > 0' directly contradicts it, so this must be caught as
// provably false.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

int
identity (int const x)
  pre<sc::proven_conveyor_v> (x < 0)
  post<sc::proven_conveyor_v> (x > 0) // { dg-error "provably false" }
                                       // { dg-message "established \[^\n\]*" "established fact" { target *-*-* } .-1 }
{
  return x;
}

int main () { return identity (-1); }
