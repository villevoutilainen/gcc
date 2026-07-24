// D4324: a contract predicate is not wrapped in exception-to-violation
// translation.  A noexcept function carrying a contract keeps noexcept: an
// exception thrown while evaluating the predicate propagates as an ordinary
// exception and is stopped at the noexcept boundary (eh_must_not_throw),
// rather than being caught (catch(...) / __cxa_begin_catch) and turned into a
// contract violation via a __tu_has_violation_exception entry point.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce -fdump-tree-gimple" }

#include <contracts>

// This test is compile-only and never links the runtime contracts support
// library (libstdc++exp), but the bare pre() below resolves to default_v,
// whose operator() calls these two library-only entry points; under
// -pedantic-errors that flags them "used but never defined".  Trivial local
// definitions sidestep that without affecting anything this test actually
// checks (the noexcept/EH boundary around evaluating the predicate, not
// what these two functions do).
namespace std { namespace contracts {
void __d4324_log_violation (const char*, std::source_location) noexcept {}
[[noreturn]] void __d4324_terminate () noexcept { __builtin_trap (); }
} }

bool maythrow (int);		// may throw: not noexcept

int f (int x) noexcept pre (maythrow (x)) { return x; }

// The noexcept boundary is preserved.
// { dg-final { scan-tree-dump "eh_must_not_throw" "gimple" } }
// No catch(...) is emitted to translate a predicate exception.
// { dg-final { scan-tree-dump-not "__cxa_begin_catch" "gimple" } }
// The exception-translation entry point is gone entirely.
// { dg-final { scan-tree-dump-not "__tu_has_violation_exception" "gimple" } }
