// D4324: an empty '<>' control specifier on pre/post/contract_assert
// resolves to std::contracts::default_v, exactly like the bare form with
// no '<...>' at all -- pre<>(cond) === pre(cond), post<>(r: cond) ===
// post(r: cond), contract_assert<>(cond) === contract_assert(cond).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce -fdump-tree-gimple" }

#include <contracts>

// This test is compile-only and never links the runtime contracts support
// library (libstdc++exp); see d4324-default-v-unification.C for why these
// two trivial local definitions are needed under -pedantic-errors.
namespace std { namespace contracts {
void __d4324_log_violation (const char*, std::source_location) noexcept {}
[[noreturn]] void __d4324_terminate () noexcept { __builtin_trap (); }
} }

int f (int x) pre<> (x > 0) { return x; }
int g (int x) post<> (r: r > 0) { return x; }

void h (int x)
{
  contract_assert<> (x > 0);
}

// Each of pre<>, post<> and contract_assert<> calls the implicit
// default_v control object on violation (matching only the call sites,
// "operator() (&...", not the one dump of operator()'s own definition) ...
// { dg-final { scan-tree-dump-times "default_control::operator\\(\\) \\(&" 3 "gimple" } }
// ... instead of the built-in violation entry point.
// { dg-final { scan-tree-dump-not "__tu_has_violation" "gimple" } }
