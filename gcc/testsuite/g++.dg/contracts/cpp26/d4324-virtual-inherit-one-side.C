// D4324: inherited() true for exactly one side (client here) -- the
// inherited copy attached to Derived::f fires only from the
// caller-side wrapper built for the statically-named call; it must
// not also fire from Derived::f's own definition-side body, since
// inherited() denies that side.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int base_calls = 0;

struct base_probe {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool inherited (sc::assertion_static_info info)
  { return info.side () == sc::assertion_check_side::client; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    base_calls++;
    if (!ctx.check ())
      __builtin_abort ();
  }
};

inline constexpr base_probe base_probe_v{};

struct Base {
  virtual int f (int x) pre<base_probe_v>(x >= 0) { return x; }
  virtual ~Base () {}
};

struct Derived : Base {
  int f (int x) override { return x * 2; }
};

int main ()
{
  Derived d;
  int r = d.f (5);
  if (r != 10)
    __builtin_abort ();

  // Fires once, from the client-side wrapper only -- not twice (which
  // would mean the definition side wrongly also ran the check).
  if (base_calls != 1)
    __builtin_abort ();

  return 0;
}
