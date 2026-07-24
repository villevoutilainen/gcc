// D4324: a contract violation discovered while constant-evaluating a
// control-object dispatch is diagnosed via the exact same
// manifestly-constant-evaluated, quiet-independent decision
// [basic.contract.eval] already requires for a bare (control-object-less)
// contract (see check_for_failed_contracts in gcc/cp/constexpr.cc) --
// reached here via a recognized library entry point,
// __d4324_consteval_diagnose_violation, that each control type's
// operator() calls under "if consteval" with its own terminating verdict.
// mandatory_v always terminates at runtime, so this is a hard error here
// too. See d4324-constexpr-violation-review.C for review_v, which never
// terminates and so only warns here, with the value still usable.
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

// The "non-constant condition for static assertion" diagnostic is anchored
// here, at the point of use; the "contract predicate is false in constant
// expression" diagnostic is reported inside <contracts> itself (at the
// __d4324_consteval_diagnose_violation call site operator() reaches), not
// at any line in this file, so it's matched by message only (line 0), not
// by position.
static_assert (f (-1) == -1);	// { dg-error "non-constant condition" }
// { dg-error "contract predicate is false in constant expression" "" { target *-*-* } 0 }
