// D4324: base_contract<Base>() naming a real base that doesn't itself
// declare a matching override is a hard error -- it must name the
// class that actually declares the override, not merely some ancestor
// that happens to inherit one transitively.
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

struct Root { virtual int f (int x) pre<probe_v>(x >= 0) { return x; } virtual ~Root(){} };
// Middle does not itself declare/override f at all.
struct Middle : Root { };

struct Derived : Middle {
  int f (int x) override
    pre<probe_v>(sc::base_contract<Middle>()) // { dg-error "does not declare an override of" }
  { return x * 2; }
};
