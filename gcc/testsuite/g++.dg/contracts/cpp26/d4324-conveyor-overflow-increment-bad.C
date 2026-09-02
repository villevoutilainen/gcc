// D4324/P2680 item 8's third mandatory scan: signed-integer-overflow-
// capable arithmetic. The motivating case (see .claude/plans/lazy-
// stirring-pearl.md): a post-increment inside the very condition being
// trusted as a conveyor fact -- x is unconstrained, so x++ risks
// overflow at TYPE_MAX, and no established fact (numeric or type-bound
// witness, see oa_type_bound_fact's own comment) rescues it.
//
// Also now correctly rejected for a second, independent reason: a
// direct mutation of X (a received, non-owned parameter) inside
// conveyor-flavored condition text is unconditionally disallowed
// regardless of its own overflow-safety (2026-09-02) -- both
// diagnostics are genuinely true here, so both are expected.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

int g ();

void f (int x)
pre<std::contracts::conveyor_assert_v>(x++ < 2048) // { dg-error "increment of .x. not provably free of overflow" }
                                                    // { dg-error "increment of .x. not permitted in a conveyor predicate" "" { target *-*-* } .-1 }
{}

void h ()
{
  f (g ());
}
