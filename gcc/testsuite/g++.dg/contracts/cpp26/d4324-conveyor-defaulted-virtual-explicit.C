// D4324: a defaulted virtual function declared 'conveyor' stays conveyor,
// and the override rule applies to it: an override must itself be
// declared 'conveyor' -- an implicitly-declared override can't be, since
// a defaulted virtual is conveyor only if declared so.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct B { virtual ~B () conveyor = default; };
struct D : B { ~D () conveyor = default; };	// OK
struct E : B { };				// { dg-error "must itself be declared .conveyor." }
struct F : B { ~F () = default; };		// { dg-error "must itself be declared .conveyor." }
