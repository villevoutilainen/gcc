// D4324/P2680 item 8's third mandatory scan: the reference-parameter
// companion to d4324-conveyor-overflow-increment-bad.C -- confirms the
// reference-read stripping fix (see the -conjunct-order-reference-ok.C
// sibling) doesn't overcorrect into silently accepting every reference
// increment: with no bounding conjunct at all, X is exactly as
// unconstrained as the value-parameter case, and must still be
// correctly rejected.
//
// Also now correctly rejected for a second, independent reason -- see
// the value-parameter sibling's own updated comment.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

int g ();

void f (int& x)
pre<std::contracts::conveyor_assert_v>(x++ < 2048) // { dg-error "increment of .x. not provably free of overflow" }
                                                    // { dg-error "increment of .x. not permitted in a conveyor predicate" "" { target *-*-* } .-1 }
{}

void h ()
{
  int v = g ();
  f (v);
}
