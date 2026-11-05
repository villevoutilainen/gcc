// D4324 analog of BZ121936-workaround-noipa.C: a named control object's
// operator() (default_control here) is an ordinary inline library function,
// visible/inlinable in every TU. Once cfg() constant-folds under enforce,
// its else-branch's call to __d4324_terminate becomes unconditional; without
// __d4324_terminate_wrapper's noipa boundary, IPA infers f's return value is
// always non-null (attaching returns_nonnull to f) and elides the redundant
// null-check below entirely -- sound only within the TU that compiled f,
// since cfg() reflects that TU's own -fcontract-evaluation-semantic=, which
// may legitimately differ between TUs (the same hazard
// -fcontracts-conservative-ipa exists to prevent leaking across TU
// boundaries for the P2900 built-in path).
// { dg-additional-options "-O3 -fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce -fdump-tree-optimized" }
// { dg-do compile { target c++26 } }

#include <contracts>

// This test is compile-only and never links the runtime contracts support
// library (libstdc++exp), but default_control's operator() calls the
// library-only __d4324_terminate in its non-consteval branch; under
// -pedantic-errors that flags it "used but never defined".  A trivial local
// definition sidesteps that without affecting anything this test actually
// checks (the IPA/noipa codegen shape, not runtime handling).
namespace std { namespace contracts {
[[noreturn]] void __d4324_terminate () noexcept { __builtin_trap (); }
} }

namespace sc = std::contracts;

[[gnu::used, gnu::noinline]]
inline int* f(int *y) pre<sc::default_v>(y != nullptr) {
  return y;
}

int foo(int *x) {
    if (f(x) == nullptr)
    {
        return 0;
    }
    return *x;
}

// Check that neither null check is optimised away: f's own check (compiled
// from the control-object dispatch as a positive "if (y != 0B)" guarding the
// violation branch) and foo's redundant, separate check afterward (compiled
// as "if (f(x) == 0B)") must both still be present -- two independent tests
// against a null pointer, not one shape repeated (unlike the P2900 built-in
// path's contract_assert codegen, D4324's is phrased positively).
// { dg-final { scan-tree-dump-times {if \([^\n]*0B\)} 2 "optimized" } }
