// D4324: base_contract<Base>() used in a pre<> where Base's own
// override has no precondition at all -- evaluates to true (vacuously)
// rather than erroring, so a composed expression doesn't need every
// ancestor to declare a matching contract kind.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int calls = 0;

struct probe {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    calls++;
    if (!ctx.check ())
      __builtin_abort ();
  }
};

inline constexpr probe probe_v{};

// Base has no precondition at all on f.
struct Base {
  virtual int f (int x) { return x; }
  virtual ~Base () {}
};

struct Derived : Base {
  int f (int x) override
    pre<probe_v>(sc::base_contract<Base>())
  { return x * 2; }
};

int main ()
{
  Derived d;
  int r = d.f (-999); // would fail any ordinary "x >= 0" precondition
  if (r != -1998)
    __builtin_abort ();
  if (calls != 1)
    __builtin_abort ();
  return 0;
}
