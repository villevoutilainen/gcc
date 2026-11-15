// D4324: 'x.f == C' false, or 'x.f != C' true, with C at an end of the
// field's known range narrows that end, as for a variable.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>
namespace sc = std::contracts;

void lt10 (int v) pre<sc::proven_conveyor_v> (v < 10);
void gt0 (int v) pre<sc::proven_conveyor_v> (v > 0);
void gem1 (int v) pre<sc::proven_conveyor_v> (v > -1);
struct X { int f; };

// A value excluded at an end of a known range narrows it, for a field as
// for a variable.
void var_hi (int n) pre<sc::proven_conveyor_v> (n >= 0 && n <= 10) { if (n == 10) return; lt10 (n); }
void fld_hi (X x) pre<sc::proven_conveyor_v> (x.f >= 0 && x.f <= 10) { if (x.f == 10) return; lt10 (x.f); }
void fld_lo (X x) pre<sc::proven_conveyor_v> (x.f >= 0 && x.f <= 10) { if (x.f == 0) return; gt0 (x.f); }
void fld_ne (X x) pre<sc::proven_conveyor_v> (x.f >= 0 && x.f <= 10) { if (x.f != 10) lt10 (x.f); }
void fld_arith (X x) pre<sc::proven_conveyor_v> (x.f >= 0 && x.f <= 10) { if (x.f == 0) return; gem1 (x.f - 1); }
void ptr_hi (const X *p) pre<sc::proven_conveyor_v> (std::is_object_address (p)) pre<sc::proven_conveyor_v> (p->f >= 0 && p->f <= 10) { if (p->f == 10) return; lt10 (p->f); }
// A by-value parameter's field is known from the precondition.
void byval (X x) pre<sc::proven_conveyor_v> (x.f >= 0 && x.f <= 10) { gem1 (x.f); }
// A value inside the range: not an end.
void fld_mid (X x) pre<sc::proven_conveyor_v> (x.f >= 0 && x.f <= 10) { if (x.f == 5) return; lt10 (x.f); } // { dg-error "cannot prove" }
