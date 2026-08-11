// D4324: std::contracts::base_contract<Base>() used inside a
// postcondition -- confirms it evaluates Base's own postcondition-set
// with Derived's own actual result value substituted in.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int calls = 0;

struct probe {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return true; }
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

struct Base {
  virtual int f (int x) post<probe_v>(r: r >= 0) { return x; }
  virtual ~Base () {}
};

struct Derived : Base { Derived () {}
  int f (int x) override
    post<probe_v>(r: sc::base_contract<Base>() && r < 1000)
  { return x * 2; }
};

int main ()
{
  Derived d;
  int r = d.f (5);
  if (r != 10)
    __builtin_abort ();
  if (calls != 1)
    __builtin_abort ();
  return 0;
}
