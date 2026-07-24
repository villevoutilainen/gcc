// D4324: naming a control object on a contract inside a constexpr function
// actually invokes that object's protocol during constant evaluation
// (is_ignored/operator()), instead of falling back to the old P2900
// built-in evaluation-semantic path the way it used to.  A satisfied
// predicate compiles cleanly, proving the dispatch works end to end for
// the success path.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }

#include <contracts>

// This test is compile-only and never links the runtime contracts support
// library (libstdc++exp), but mandatory_v's operator() calls these two
// library-only entry points in its non-consteval branch; under
// -pedantic-errors that flags them "used but never defined".  Trivial local
// definitions sidestep that without affecting anything this test actually
// checks (constant-evaluation dispatch, not runtime violation handling).
namespace std { namespace contracts {
void __d4324_log_violation (const char*, std::source_location) noexcept {}
[[noreturn]] void __d4324_terminate () noexcept { __builtin_trap (); }
} }

namespace sc = std::contracts;

constexpr int f (int x) pre<sc::mandatory_v>(x >= 0) { return x; }

static_assert (f (1) == 1);
static_assert (f (0) == 0);

constexpr void
g (int x)
{
  contract_assert<sc::mandatory_v>(x >= 0 || x < 0);
}

static_assert ((g (-5), true));
