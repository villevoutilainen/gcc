// D4324: inheritance works at whole-precondition-set granularity -- if
// Base::f has multiple preconditions whose control objects disagree on
// inherited() for a given side, that is a hard error, not something
// silently resolved.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

struct yes_probe {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool inherited (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const { ctx.check (); }
};

struct no_probe {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool inherited (sc::assertion_static_info) { return false; }
  void operator() (const sc::assertion_context& ctx) const { ctx.check (); }
};

inline constexpr yes_probe yes_probe_v{};
inline constexpr no_probe no_probe_v{};

struct Base {
  virtual int f (int x)
    pre<yes_probe_v>(x >= 0)
    pre<no_probe_v>(x < 100)
  { return x; }
  virtual ~Base () {}
};

struct Derived : Base {
  int f (int x) override { return x * 2; } // { dg-error "disagreement between inherited base contracts" }
};
