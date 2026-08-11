// D4324: inherited() true for both sides -- the synthesized copy
// attached to Derived::f is genuinely eligible on both. A statically-
// named direct call goes through both Derived::f's own caller-side
// wrapper (client) and its own body (definition), so both fire; a
// virtual call through Base* only ever reaches Derived::f's body
// (definition) -- there is no caller-side wrapper for Base::f's own,
// unrelated, unforced contract, since inherited() only governs the
// synthesized copy, not Base::f's own eligibility.
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
  static constexpr bool inherited (sc::assertion_static_info) { return true; }
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

struct Derived : Base { Derived () {}
  int f (int x) override { return x * 2; }
};

int main ()
{
  // Direct, statically-named call: both the caller-side wrapper
  // (client) and Derived::f's own body (definition) check the
  // inherited copy.
  Derived d;
  int r1 = d.f (5);
  if (r1 != 10)
    __builtin_abort ();
  if (base_calls != 2)
    __builtin_abort ();

  base_calls = 0;

  // Through Base*: only Derived::f's own body (definition), reached by
  // dynamic dispatch, checks the inherited copy -- Base::f's own,
  // unforced contract has no client-side eligibility of its own.
  Base* b = new Derived ();
  int r2 = b->f (5);
  if (r2 != 10)
    __builtin_abort ();
  if (base_calls != 1)
    __builtin_abort ();

  delete b;
  return 0;
}
