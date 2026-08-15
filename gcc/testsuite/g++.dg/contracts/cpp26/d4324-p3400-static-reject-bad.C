// D4324/P3400: two genuinely static, build-independent ill-formedness
// checks -- entirely unrelated to any -fcontract-evaluation-semantic=
// -- both real compile errors via combined_label's own class-template
// static_assert: two labels whose fixed allowed_semantics sets don't
// overlap at all, and two labels whose fixed dimensions overlap.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace P3400 = std::contracts::P3400;

// always_ignore restricts to {ignore}; terminating restricts to
// {enforce, quick_enforce} -- disjoint, so their combination's own
// allowed_semantics would be empty. The primary static_assert failure
// cascades into secondary errors (is_ignored's own use of the
// now-ill-formed allowed_semantics member fails to constant-fold in
// turn) -- matched by message text rather than by line, same
// technique used for the Phase 0 tests.
// { dg-error "combining labels with disjoint allowed_semantics is ill-formed" "" { target *-*-* } 0 }
// { dg-error "does not produce a constant expression" "" { target *-*-* } 0 }
// { dg-error "uncaught exception" "" { target *-*-* } 0 }
int
f (int x) pre<P3400::always_ignore | P3400::terminating>(x > 0)
{ return x; }

// opt and audit share the runtime_cost dimension -- combining them is
// ill-formed regardless of any build configuration.
// { dg-error "combining labels with overlapping dimensions is ill-formed" "" { target *-*-* } 0 }
int
g (int x) pre<P3400::opt | P3400::audit>(x > 0)
{ return x; }
