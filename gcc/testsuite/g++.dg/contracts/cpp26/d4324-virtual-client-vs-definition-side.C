// D4324: the caller-side (client) check and the definition-side check
// for a virtual call are two genuinely different functions, exactly as
// they should be: the caller-side wrapper checks the contract of the
// statically-chosen function (Base::f, forced client-side here via
// force_client_side_check), while the actual final overrider reached
// by dynamic dispatch (Derived::f) is responsible for its own,
// independent, definition-side contract.
//
// The wrapper's own internal call to the real function must therefore
// still be a genuine virtual call: if it silently degraded to a direct,
// non-virtual call to Base::f (the historical bug this test guards
// against), both the return value and derived_calls below would give
// it away.
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
  static constexpr bool force_client_side_check (sc::assertion_static_info) { return true; }
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

// Declaration and definition split, matching d4324-client-check.C: the
// caller-side wrapper is built from the declaration seen at the call
// site in main(), Base::f's body is defined afterward.
struct Base {
  virtual int f (int x) pre<base_probe_v>(x >= 0);
  virtual ~Base () {}
};

int
Base::f (int x)
{
  return x;
}

struct Derived : Base {
  int f (int x) override pre<derived_probe_v>(x >= 0) { return x * 2; }
};

int main ()
{
  Base* b = new Derived ();
  int r = b->f (5);

  // Derived::f actually ran (not Base::f): proves the wrapper's
  // internal call genuinely dispatched through the vtable.
  if (r != 10)
    __builtin_abort ();

  // Base::f's contract (forced client-side) fired exactly once, from
  // the caller-side wrapper built for the statically-chosen Base::f.
  if (base_calls != 1)
    __builtin_abort ();

  // Derived::f's own, independent, unforced contract fired exactly
  // once, from the final overrider's own definition-side check.
  if (derived_calls != 1)
    __builtin_abort ();

  delete b;
  return 0;
}
