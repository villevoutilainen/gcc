// D4324: callers of the implicit copy and assignment of a class with a
// virtual base prove that the source is an object, as for any defaulted
// copy whose body needs it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct H { int v; H (int x) : v (x) {} };
struct B1 : virtual H { B1 (int x) : H (x) {} };
struct B2 : virtual H { B2 (int x) : H (x) {} };
struct D : B1, B2 { D (int x) : H (x), B1 (x), B2 (x) {} };

D copy (const D &d) { return d; }	// { dg-error "is_object_address" }
