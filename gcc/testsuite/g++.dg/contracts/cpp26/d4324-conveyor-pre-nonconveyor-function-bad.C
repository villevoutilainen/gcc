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
//
// The 'x < 2048' warning's own expected text was updated 2026-08-27:
// a reference parameter's own read (INDIRECT_REF(x)) wasn't recognized
// by oa_match_simple_comparison, so this conjunct fell through the
// canonical, shared oa_handle_precondition_simple_range_obligation
// mechanism entirely (a value parameter's identical shape already used
// it) down to a generic fallback that happened to print the literal
// declared condition text. Fixing that reference-read recognition gap
// (see .claude/plans/lazy-stirring-pearl.md) routes this conjunct
// through the same, correct, standard mechanism a value parameter's
// 'x < 2048' already used, whose own message names the call-site
// argument instead ('%qE satisfies', more actionable) -- the message
// text changed, not the underlying diagnostic's own validity.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>

void f (int& x) pre<std::contracts::conveyor_assert_v> (x < 2048)
{}

void h (int* p)
{
  f (*p); // { dg-error "cannot prove .is_object_address. for .\\* p." }
	  // { dg-message "no fact relating this value" "unprovable reason" { target *-*-* } .-1 }
	  // { dg-warning "cannot verify that .\\* p. satisfies" "" { target *-*-* } .-2 }
}
