// D4324: two distinct direct bases each offering a distinct
// inheritable contract for the same override is a hard error
// (ambiguous), rather than silently picking one.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

struct probe_a {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool inherited (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const { ctx.check (); }
};

struct probe_b {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool inherited (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const { ctx.check (); }
};

inline constexpr probe_a probe_a_v{};
inline constexpr probe_b probe_b_v{};

struct BaseA { virtual int f (int x) pre<probe_a_v>(x >= 0) { return x; } virtual ~BaseA(){} };
struct BaseB { virtual int f (int x) pre<probe_b_v>(x >= 0) { return x; } virtual ~BaseB(){} };

struct Derived : BaseA, BaseB {
  int f (int x) override { return x * 2; } // { dg-error "ambiguous inherited contract" }
};
