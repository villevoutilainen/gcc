// D4324: base_contract<Base>() may only be used inside a contract of a
// virtual member function -- an ordinary, non-overriding member
// function is rejected.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

struct probe {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void operator() (const sc::assertion_context& ctx) const { ctx.check (); }
};

inline constexpr probe probe_v{};

struct Base { virtual int f (int x) pre<probe_v>(x >= 0) { return x; } virtual ~Base(){} };

struct Derived : Base {
  int f (int x) override pre<probe_v>(x >= 0) { return x * 2; }
  // Non-virtual, non-overriding function -- illegal use.
  int g (int x) pre<probe_v>(sc::base_contract<Base>()) // { dg-error "may only be used in a contract of a virtual member function" }
  { return x; }
};
