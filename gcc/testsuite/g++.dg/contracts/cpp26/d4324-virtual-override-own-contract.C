// D4324: Base::f and an actual override Derived::f each declare their
// own, independent control-object precondition, with no inheritance
// between them.  A call through a Base* names Base::f, so the caller
// checks Base::f's contract; the function that runs checks its own:
// Base::f's again for a Base instance, Derived::f's for a Derived one.
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
  Base* b = new Base ();
  if (b->f (5) != 5)
    __builtin_abort ();
  if (base_calls != 2 || derived_calls != 0)
    __builtin_abort ();
  delete b;

  Base* d = new Derived ();
  if (d->f (5) != 10)
    __builtin_abort ();
  if (base_calls != 3 || derived_calls != 1)
    __builtin_abort ();
  delete d;

  return 0;
}
