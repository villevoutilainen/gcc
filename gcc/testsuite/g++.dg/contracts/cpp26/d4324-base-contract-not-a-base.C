// D4324: base_contract<Base>() naming a class that isn't actually a
// base of the enclosing class is a hard error.
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

struct Unrelated { int f (int x) { return x; } };

struct Base { virtual int f (int x) pre<probe_v>(x >= 0) { return x; } virtual ~Base(){} };

struct Derived : Base {
  int f (int x) override
    pre<probe_v>(sc::base_contract<Unrelated>()) // { dg-error "is not a base of" }
  { return x * 2; }
};
