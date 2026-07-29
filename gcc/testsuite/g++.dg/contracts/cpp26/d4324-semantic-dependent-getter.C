// D4324: constify (like assumable, omit_comment, omit_source_location,
// force_client_side_check, force_definition_side_check) takes the TU's
// evaluation_semantic, exactly like is_ignored, precisely so a control
// type can answer differently depending on it, instead of being pinned
// to one fixed compile-time answer -- the motivating flexibility for
// moving these from static constexpr bool fields to static constexpr
// bool getter functions.
//
// This is a compile-only proof that the getter genuinely receives and
// branches on the real TU semantic, not a constant folded away
// regardless of it: built with -fcontract-evaluation-semantic=observe,
// only the control type whose constify(c) tests true for *this* build's
// actual semantic restores the const overload; the other, which tests
// for a different semantic, does not -- so the two controls must
// disagree within this single compile, exactly the way
// d4324-cfg-observe.C already proves for is_ignored.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe" }

#include <contracts>

// This test is compile-only and never links the runtime contracts support
// library (libstdc++exp); see d4324-constify-off.C for why these trivial
// local definitions are needed under -pedantic-errors.
namespace std { namespace contracts {
void __d4324_log_violation (const char*, std::source_location) noexcept {}
[[noreturn]] void __d4324_terminate () noexcept { __builtin_trap (); }
} }

namespace sc = std::contracts;

struct constify_under_observe {
  static constexpr bool is_ignored (sc::evaluation_semantic) { return false; }
  static constexpr bool constify (sc::evaluation_semantic c)
  { return c == sc::evaluation_semantic::observe; }
  static constexpr bool assumable (sc::evaluation_semantic) { return false; }
  void operator() (const sc::assertion_context&) const {}
};

struct constify_under_enforce {
  static constexpr bool is_ignored (sc::evaluation_semantic) { return false; }
  static constexpr bool constify (sc::evaluation_semantic c)
  { return c == sc::evaluation_semantic::enforce; }
  static constexpr bool assumable (sc::evaluation_semantic) { return false; }
  void operator() (const sc::assertion_context&) const {}
};

inline constexpr constify_under_observe constify_under_observe_v{};
inline constexpr constify_under_enforce constify_under_enforce_v{};

struct S { bool probe (); bool probe () const; };
struct T { bool probe (); bool probe () const; };

// Built under =observe: constify_under_observe's constify(observe) is
// true, so this restores the const overload ...
int f (T t) pre<constify_under_observe_v>(t.probe ()) { return 0; }
// ... but constify_under_enforce's constify(observe) is false (this
// build's actual semantic is observe, not enforce), so this stays
// non-const, exactly like the default/no-control-object case.
int g (S s) pre<constify_under_enforce_v>(s.probe ()) { return 0; }

// { dg-final { scan-assembler "_ZNK1T5probeEv" } }
// { dg-final { scan-assembler "_ZN1S5probeEv" } }
// { dg-final { scan-assembler-not "_ZNK1S5probeEv" } }
