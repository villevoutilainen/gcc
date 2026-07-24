// D4324: a contract violation discovered while constant-evaluating a
// control-object dispatch fails via the control-object protocol itself --
// mandatory_v's operator() genuinely runs at compile time and hits
// __d4324_consteval_violation (declared, deliberately never defined, only
// ever reached along an "if consteval" branch) -- rather than the old
// bespoke "contract predicate is false in constant expression" message,
// which is now only ever produced on the P2900 built-in
// (-fcontract-control-objects off) path.  See
// d4324-constexpr-violation-review.C for review_v, which fails identically
// despite its different (log-only, non-terminating) runtime behavior.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }

#include <contracts>

// This test is compile-only and never links the runtime contracts support
// library (libstdc++exp), but mandatory_v's operator() calls these two
// library-only entry points in its non-consteval branch; under
// -pedantic-errors that flags them "used but never defined".  Trivial local
// definitions sidestep that without affecting anything this test actually
// checks (the compile-time violation path, not runtime handling).
namespace std { namespace contracts {
void __d4324_log_violation (const char*, std::source_location) noexcept {}
[[noreturn]] void __d4324_terminate () noexcept { __builtin_trap (); }
} }

namespace sc = std::contracts;

constexpr int f (int x) pre<sc::mandatory_v>(x >= 0) { return x; }

// The primary diagnostic is anchored here, at the point of use; the
// secondary "call to non-'constexpr' function ... __d4324_consteval_violation"
// diagnostic is reported inside <contracts> itself (at the "if consteval"
// call site operator() reaches), not at any line in this file, so it's
// matched by message only (line 0), not by position.
static_assert (f (-1) == -1);	// { dg-error "non-constant condition" }
// { dg-error "call to non-.constexpr. function" "" { target *-*-* } 0 }
