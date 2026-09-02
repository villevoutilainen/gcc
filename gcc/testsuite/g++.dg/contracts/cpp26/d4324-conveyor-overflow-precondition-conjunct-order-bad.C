// D4324/P2680 item 8's overflow scan: the mirror-image conjunct order --
// 'x++' (now the *first* conjunct) has nothing preceding it to refine
// from, so it correctly still stays rejected, confirming the new
// left-to-right refinement doesn't over-generalize.
//
// Also now correctly rejected for a second, independent reason -- see
// d4324-conveyor-overflow-increment-bad.C's own updated comment.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

void f (int x)
pre<std::contracts::conveyor_assert_v>(x++ < 2048 && x < 100000) // { dg-error "increment of .x. not provably free of overflow" }
                                                                  // { dg-error "increment of .x. not permitted in a conveyor predicate" "" { target *-*-* } .-1 }
{}

int main () { f (1); return 0; }
