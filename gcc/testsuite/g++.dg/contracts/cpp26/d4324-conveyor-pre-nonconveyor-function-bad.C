// D4324/P2680: a conveyor-flavored pre<> on a function that is NOT
// itself declared 'conveyor' must still get the same implicit
// is_object_address(&x) obligation on its own reference parameters a
// real 'conveyor' function already gets -- a conveyor restriction
// always applies to both conveyor-declared functions and conveyor
// predicates (contract condition text), never one alone. Before this
// fix, oa_synthesize_implicit_reference_safety_preconditions was gated
// purely on DECL_DECLARED_CONVEYOR_P, so f's own reference parameter
// 'x' got no such obligation at all: calling f with an entirely
// unproven '*p' produced no diagnostic whatsoever, even though 'x <
// 2048' (f's own explicit, conveyor-active conjunct) was already
// checked at every call site regardless of the 'conveyor' keyword.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>

void f (int& x) pre<std::contracts::conveyor_assert_v> (x < 2048)
{}

void h (int* p)
{
  f (*p); // { dg-error "cannot prove .is_object_address. for .\\* p." }
	  // { dg-message "no fact relating this value" "unprovable reason" { target *-*-* } .-1 }
	  // { dg-warning "cannot verify that .\\(x < 2048\\). satisfies" "" { target *-*-* } .-2 }
}
