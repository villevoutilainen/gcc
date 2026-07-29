// D4324: constification is off by default, so a contract predicate binds the
// same overload the function body would - no silent divergence under overload
// resolution.  Naming a control type whose constify member is true restores
// constification for the assertions that name it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }

#include <contracts>

// This test is compile-only and never links the runtime contracts support
// library (libstdc++exp), but the bare pre() below resolves to default_v,
// whose operator() calls these two library-only entry points; under
// -pedantic-errors that flags them "used but never defined".  Trivial local
// definitions sidestep that without affecting anything this test actually
// checks (constification / overload binding, not violation handling).
namespace std { namespace contracts {
void __d4324_log_violation (const char*, std::source_location) noexcept {}
[[noreturn]] void __d4324_terminate () noexcept { __builtin_trap (); }
} }

struct constified {
  static constexpr bool is_ignored (std::contracts::evaluation_semantic) { return false; }
  static constexpr bool constify (std::contracts::evaluation_semantic) { return true; }
  static constexpr bool assumable (std::contracts::evaluation_semantic) { return false; }
  void operator() (const std::contracts::assertion_context&) const {}
};

inline constexpr constified constified_v{};

struct S { bool probe (); bool probe () const; };
struct T { bool probe (); bool probe () const; };

int f (S s) pre (s.probe ()) { return 0; }		// default: non-const
int g (T t) pre<constified_v>(t.probe ()) { return 0; }	// opt-in: const

// Default: the predicate binds the non-const overload the body would bind ...
// { dg-final { scan-assembler "_ZN1S5probeEv" } }
// ... and never the const overload.
// { dg-final { scan-assembler-not "_ZNK1S5probeEv" } }
// A constify=true control restores constification: the const overload binds.
// { dg-final { scan-assembler "_ZNK1T5probeEv" } }
