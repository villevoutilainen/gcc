// D4324: with control objects enabled, a bare pre/post/contract_assert (no
// named control) uses std::contracts::default_v exactly as if
// '<std::contracts::default_v>' had been written: the compiler makes a
// single call to default_control::operator() on violation, replacing the
// built-in __tu_has_violation semantic switch used without
// -fcontract-control-objects.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce -fdump-tree-gimple" }

#include <contracts>

// This test is compile-only and never links the runtime contracts support
// library (libstdc++exp), but the bare pre() below resolves to default_v,
// whose operator() calls these two library-only entry points; under
// -pedantic-errors that flags them "used but never defined".  Trivial local
// definitions sidestep that without affecting anything this test actually
// checks (which entry point gets called, not what it does).
namespace std { namespace contracts {
void __d4324_log_violation (const char*, std::source_location) noexcept {}
[[noreturn]] void __d4324_terminate () noexcept { __builtin_trap (); }
} }

int f (int x) pre (x > 0) { return x; }

// The implicit default_v control object is called on violation ...
// { dg-final { scan-tree-dump "default_control::operator" "gimple" } }
// ... instead of the built-in violation entry point.
// { dg-final { scan-tree-dump-not "__tu_has_violation" "gimple" } }
