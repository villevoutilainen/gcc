// D4324: a contract predicate is not wrapped in exception-to-violation
// translation.  A noexcept function carrying a contract keeps noexcept: an
// exception thrown while evaluating the predicate propagates as an ordinary
// exception and is stopped at the noexcept boundary (eh_must_not_throw),
// rather than being caught (catch(...) / __cxa_begin_catch) and turned into a
// contract violation via a __tu_has_violation_exception entry point.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce -fdump-tree-gimple" }

#include <contracts>

bool maythrow (int);		// may throw: not noexcept

int f (int x) noexcept pre (maythrow (x)) { return x; }

// The noexcept boundary is preserved.
// { dg-final { scan-tree-dump "eh_must_not_throw" "gimple" } }
// No catch(...) is emitted to translate a predicate exception.
// { dg-final { scan-tree-dump-not "__cxa_begin_catch" "gimple" } }
// The exception-translation entry point is gone entirely.
// { dg-final { scan-tree-dump-not "__tu_has_violation_exception" "gimple" } }
