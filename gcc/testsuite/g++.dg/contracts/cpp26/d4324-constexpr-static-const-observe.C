// D4324: companion to d4324-constexpr-static-const-enforce.C -- under
// observe, default_v's violation only warns (terminating = cfg() !=
// observe = false here), and, exactly like a bare contract's observe
// semantic, the already-computed value is kept: y is still successfully
// constant-initialized to -42 (verified by the static_assert below and,
// for a stronger check, dg-final scans confirming there is no dynamic
// (runtime, .init_array-registered) initialization of y at all -- it is
// folded directly into the binary).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe -O2 -fdump-tree-optimized" }

#include <contracts>

namespace std { namespace contracts {
void __d4324_log_violation (const char*, std::source_location) noexcept {}
} }

namespace sc = std::contracts;

constexpr int f (int x) pre<sc::default_v>(x >= 0) { return x; }

static const int y = f (-42);
// { dg-warning "contract predicate is false in constant expression" "" { target *-*-* } 0 }

static_assert (y == -42);

int main () { return y; }

// y was constant-initialized at compile time (the warning didn't block
// it): no dynamic-initialization function is emitted for it at all.
// { dg-final { scan-tree-dump-not "_GLOBAL__sub_I" "optimized" } }
