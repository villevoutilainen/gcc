// D4324: Base::f has a control-object precondition; Derived::f
// overrides it with no contracts of its own. Step 1 does not add any
// contract inheritance (that's later work), so calling through a
// Base* to a Derived instance must run Derived::f's own (contract-free)
// body and must NOT invoke Base::f's control object at all.
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
  Base* d = new Derived ();
  if (d->f (5) != 10)
    __builtin_abort ();
  if (base_calls != 0)
    __builtin_abort ();
  delete d;
  return 0;
}
