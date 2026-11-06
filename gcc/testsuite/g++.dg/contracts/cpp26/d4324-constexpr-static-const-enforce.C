// D4324: a control-object contract violation is diagnosed even when the
// only constant evaluation attempt is a "trial" one -- initializing an
// ordinary (non-constexpr, non-constinit) static const variable, which is
// free to silently fall back to dynamic initialization on failure in
// general, but is still manifestly-constant-evaluated per
// [expr.const]/[basic.start.static] for the purpose of this diagnostic
// (confirmed via gcc/cp/constexpr.cc's maybe_constant_init_1, which forces
// manifestly_const_eval = mce_true for any static-duration variable's
// trial initializer, independent of contracts entirely -- P2900's own
// check_for_failed_contracts already relies on exactly this for the bare
// contract case, and this test confirms the control-object path now
// matches it, instead of silently deferring to a runtime abort during
// dynamic initialization.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }

#include <contracts>

namespace sc = std::contracts;

constexpr int f (int x) pre<sc::default_v>(x >= 0) { return x; }

static const int y = f (-42);
// { dg-error "constexpr message: x >= 0 \\\[contract\\\]" "" { target *-*-* } 0 }

int main () { return y; }
