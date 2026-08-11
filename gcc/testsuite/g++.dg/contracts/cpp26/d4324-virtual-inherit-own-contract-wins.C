// D4324: an override that declares its own contract never inherits
// Base::f's contract, even though it's eligible for inheritance -- an
// override's own, explicit contract always takes precedence.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int base_calls = 0;
int derived_calls = 0;

struct base_probe {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool inherited (sc::assertion_static_info) { return true; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    base_calls++;
    if (!ctx.check ())
      __builtin_abort ();
  }
};

struct derived_probe {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    derived_calls++;
    if (!ctx.check ())
      __builtin_abort ();
  }
};

inline constexpr base_probe base_probe_v{};
inline constexpr derived_probe derived_probe_v{};

struct Base {
  virtual int f (int x) pre<base_probe_v>(x >= 0) { return x; }
  virtual ~Base () {}
};

struct Derived : Base {
  int f (int x) override pre<derived_probe_v>(x >= 0) { return x * 2; }
};

int main ()
{
  Derived d;
  int r = d.f (5);
  if (r != 10)
    __builtin_abort ();

  // Only Derived::f's own contract ran; Base::f's was never inherited.
  if (base_calls != 0)
    __builtin_abort ();
  if (derived_calls != 1)
    __builtin_abort ();

  return 0;
}
