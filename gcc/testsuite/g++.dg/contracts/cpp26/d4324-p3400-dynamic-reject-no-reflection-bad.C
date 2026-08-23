// D4324/P3400: same rejection as d4324-p3400-dynamic-reject-bad.C, but
// without -freflection -- label_base::validate falls back to its old,
// fixed, dependency-free message (naming neither the specific label nor
// the specific rejected semantic) instead of erroring or silently
// accepting. <contract_labels> itself, and every label in it, must stay
// fully usable without -freflection.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe" }

#include <contract_labels>
namespace P3400 = std::contracts::P3400;

// terminating only allows {enforce, quick_enforce}; this TU is built
// with -fcontract-evaluation-semantic=observe, outside that set.
// { dg-error "this TU's configured \\(or group-overridden\\) evaluation semantic is outside this control object's own allowed_semantics set" "" { target *-*-* } 0 }
int
f (int x) pre<P3400::terminating>(x > 0)
{ return x; }
