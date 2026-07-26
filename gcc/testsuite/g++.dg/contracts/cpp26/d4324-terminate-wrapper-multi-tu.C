// D4324: std::contracts::__d4324_terminate_wrapper (the [[gnu::noipa]]
// PR121936 workaround default_control/mandatory's operator() calls on a
// violation) must be inline: it's an ordinary namespace-scope function
// defined with a body directly in <contracts>, so every TU that includes
// <contracts> with -fcontract-control-objects and actually needs it (any
// TU using default_v/mandatory_v, including via a bare pre/post/
// contract_assert) gets its own copy. Without "inline" that's a
// multiple-definition link error as soon as two or more such TUs are
// linked together -- exactly what this test builds (see
// d4324-terminate-wrapper-multi-tu-aux.cc for the second TU).
// { dg-do link { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-additional-sources "d4324-terminate-wrapper-multi-tu-aux.cc" }

#include <contracts>

// See d4324-terminate-wrapper-multi-tu-aux.cc: same inline stand-in,
// present in both TUs, unrelated to the bug this test targets.
namespace std { namespace contracts {
inline void __d4324_log_violation (const char*, std::source_location) noexcept {}
[[noreturn]] inline void __d4324_terminate () noexcept { __builtin_trap (); }
} }

int g (int x);
int f (int x) pre (x >= 0) { return x; }

int main () { return f (1) + g (2); }
