// D4324: like d4324-constexpr-violation.C, but for review_v, whose runtime
// behavior (log the violation, but never terminate) has no faithful
// compile-time analog for the LOGGING itself -- logging is inherently an
// unrepresentable I/O side effect during constant evaluation -- but DOES
// have one for the "never terminates" part: review_v's operator() passes
// terminating=false to __d4324_consteval_diagnose_violation, so a violation
// only WARNS at compile time, exactly mirroring a bare contract's observe
// semantic -- and, just like observe, the already-computed value is kept
// and the static_assert relying on it still succeeds.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }

#include <contracts>

// This test is compile-only and never links the runtime contracts support
// library (libstdc++exp), but review_v's operator() calls this library-only
// entry point in its non-consteval branch; under -pedantic-errors that
// flags it "used but never defined".  A trivial local definition sidesteps
// that without affecting anything this test actually checks (the
// compile-time violation path, not runtime handling).
namespace std { namespace contracts {
void __d4324_log_violation (const char*, std::source_location) noexcept {}
} }

namespace sc = std::contracts;

constexpr int r (int x) pre<sc::review_v>(x >= 0) { return x; }

// Warning only, reported inside <contracts> itself (at the
// __d4324_consteval_diagnose_violation call site operator() reaches), not
// at any line in this file, so it's matched by message only (line 0), not
// by position -- and the static_assert itself must still PASS: r(-1)'s
// value (-1) is kept despite the warning, exactly like a bare contract's
// observe semantic keeps its value.
static_assert (r (-1) == -1);
// { dg-warning "contract predicate is false in constant expression" "" { target *-*-* } 0 }
