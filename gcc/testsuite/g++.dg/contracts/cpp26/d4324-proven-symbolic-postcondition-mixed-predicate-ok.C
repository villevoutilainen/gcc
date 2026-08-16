// D4324: regression test for the crux of the design correction -- a
// per-CONJUNCT, not per-postcondition, exemption. open()'s postcondition
// has two conjuncts: 'IsOpen(this)' (a call to a function declared
// 'symbolic', with no body at all -- the ONLY legitimate exemption, an
// unconditional trusted axiom) and 'this->count > 0' (an ordinary field
// comparison, checked with the same rigor a conveyor postcondition's
// conjuncts already get). Both hold here: IsOpen(this) is trusted
// regardless of whether anything establishes it, and this->count > 0 is
// genuinely provable from open()'s own body.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct F;
bool IsOpen (F*) symbolic;

struct F {
  int count;
  void open ()
    post<sc::proven_symbolic_v>(IsOpen (this) && this->count > 0)
  { count = 5; }
};

int main () { F f; f.open (); return 0; }
