// D4324/P3400: the *dynamic* check -- this TU's actual configured
// evaluation semantic must fall within a label's own allowed_semantics
// -- made a real, diagnosed compile error via label_base::validate() (a
// dedicated, optional Contract Control Object member, not a P3400 facet
// itself) and the compiler's own dispatch for it in contract_active_p/
// contract_control_bool_member: a control object opting into validate()
// and cleanly folding to false is diagnosed directly, rather than
// is_ignored() having to break its own constant-evaluation to smuggle
// the rejection through the "trait exists but doesn't fold" diagnostic
// meant for genuine control-object bugs. Unlike d4324-p3400-static-
// reject-bad.C's checks, this one genuinely depends on
// -fcontract-evaluation-semantic= and applies identically to a lone
// label or a combined one.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe -freflection" }

#include <contract_labels>
namespace P3400 = std::contracts::P3400;

// terminating only allows {enforce, quick_enforce}; this TU is built
// with -fcontract-evaluation-semantic=observe, outside that set. With
// -freflection, label_base::validate names both the specific label
// (via std::meta::display_string_of) and the specific rejected
// semantic in its own message, rather than the generic fixed text
// used when reflection isn't enabled.
// { dg-error "terminating_t rejects the configured evaluation semantic 'observe'" "" { target *-*-* } 0 }
int
f (int x) pre<P3400::terminating>(x > 0)
{ return x; }
