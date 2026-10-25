// D4324: a reference bound through a pointer nothing proves is no object
// address, a temporary or not.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct V { int c; int cat () const conveyor { return c; } };

int through (const V *p) { const V &r = *p; return r.cat (); }	// { dg-error "is_object_address" }
