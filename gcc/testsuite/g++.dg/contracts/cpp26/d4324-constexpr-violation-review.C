// D4324: like d4324-constexpr-violation.C, but for review_v, whose runtime
// behavior (log the violation, but don't terminate) has no faithful
// compile-time analog: logging is inherently an unrepresentable I/O side
// effect during constant evaluation, so review_v fails to compile
// identically to mandatory_v on a violating predicate, even though its
// runtime behavior is very different (mandatory_v terminates; review_v
// just logs and continues).
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

// The primary diagnostic is anchored here, at the point of use; the
// secondary "call to non-'constexpr' function ... __d4324_consteval_violation"
// diagnostic is reported inside <contracts> itself (at the "if consteval"
// call site operator() reaches), not at any line in this file, so it's
// matched by message only (line 0), not by position.
static_assert (r (-1) == -1);	// { dg-error "non-constant condition" }
// { dg-error "call to non-.constexpr. function" "" { target *-*-* } 0 }
