// D4324: companion to d4324-proven-symbolic-postcondition-mixed-
// predicate-ok.C -- same two-conjunct postcondition, but open()'s own
// body never sets count > 0 (still 0, its default). 'IsOpen(this)'
// stays silently trusted (the only legitimate exemption -- a call to a
// function declared 'symbolic', no body to check it against); the
// plain comparison 'this->count > 0' correctly gets diagnosed, exactly
// as it would under proven_conveyor. This is the exact per-conjunct
// split the design correction is about: the WHOLE postcondition being
// symbolic-flavored must not exempt the half that's actually checkable.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct F;
bool IsOpen (F*) symbolic;

struct F {
  int count;
  void open ()
    post<sc::proven_symbolic_v>(IsOpen (this) && this->count > 0) // { dg-error "cannot prove postcondition condition" }
  { }
};

int main () { F f; f.open (); return 0; }
