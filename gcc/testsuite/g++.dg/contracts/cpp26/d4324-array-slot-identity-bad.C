// D4324/P2680, companion to the -ok.C case: without the explicit
// assertion establishing '&st[current]''s own array-slot identity, the
// composed field access must still correctly fail -- an opaque pointer
// parameter has no traceable named-array provenance for Increment 1's
// own range-based composition to fall back on either.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct Inner { int v; };
struct Outer { Inner tickets; };

int use_val_const (const Inner& x) conveyor { return x.v; }

int
reject_unasserted_slot (Outer* state, unsigned current) conveyor
{
  Outer* const st = state;
  // No assertion establishing st[current]'s own object address here.
  return use_val_const (st[current].tickets); // { dg-error "cannot prove .is_object_address." }
                                                // { dg-error "pointer dereference of" "" { target *-*-* } .-1 }
}

int main () { return 0; }
