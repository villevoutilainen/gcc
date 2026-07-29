// D4324: postconditions inherit the same way preconditions do,
// including a correctly-synthesized fresh result-variable placeholder
// for the inherited copy attached to Derived::f.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int base_calls = 0;

struct base_probe {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return true; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool inherited (sc::assertion_static_info info)
  { return info.side () == sc::assertion_check_side::definition; }
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
  virtual int f (int x) post<base_probe_v>(r: r >= 0) { return x; }
  virtual ~Base () {}
};

struct Derived : Base {
  int f (int x) override { return x * 3; }
};

int main ()
{
  Derived d;
  int r = d.f (5);
  if (r != 15)
    __builtin_abort ();
  if (base_calls != 1)
    __builtin_abort ();
  return 0;
}
