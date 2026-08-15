// D4324/P3400: the *dynamic* check -- this TU's actual configured
// evaluation semantic must fall within a label's own allowed_semantics
// -- made a real, diagnosed compile error by the Phase 0 compiler fix
// (present-but-broken control-object trait fails loudly instead of
// silently defaulting). Unlike d4324-p3400-static-reject-bad.C's
// checks, this one genuinely depends on -fcontract-evaluation-semantic=
// and applies identically to a lone label or a combined one.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe" }

#include <contracts>
namespace P3400 = std::contracts::P3400;

// terminating only allows {enforce, quick_enforce}; this TU is built
// with -fcontract-evaluation-semantic=observe, outside that set.
// { dg-error "disallowed evaluation semantic" "" { target *-*-* } 0 }
// { dg-error "does not produce a constant expression" "" { target *-*-* } 0 }
int
f (int x) pre<P3400::terminating>(x > 0)
{ return x; }
