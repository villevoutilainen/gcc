// D4324: an override that declares no contract of its own inherits
// Base::f's precondition, and the inherited check must work even when
// Base::f's condition references a member private to Base -- Derived
// has no friend declaration, no protected access, nothing. The
// predicate is never re-hosted in Derived's context; it's called, not
// copied, so Base's own access rights are all that's needed.
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
  virtual int f (int x) pre<base_probe_v>(x >= min_) { return x; }
  virtual ~Base () {}
private:
  int min_ = 0;
};

// No friend declaration, no access grant of any kind.
struct Derived : Base {
  int f (int x) override { return x * 2; }
};

int main ()
{
  Derived d;
  int r = d.f (5);
  if (r != 10)
    __builtin_abort ();
  if (base_calls != 1)
    __builtin_abort ();
  return 0;
}
