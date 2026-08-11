// D4324: Eiffel/Ada-style postcondition strengthening -- Derived's own
// postcondition ANDs together base_contract<>() for each ancestor,
// requiring everything every one of them promised, plus its own.
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

struct X {
  virtual int f (int x) post<probe_v>(r: r >= 0) { return x; }
  virtual ~X () {}
};
struct Y : X {
  int f (int x) override post<probe_v>(r: r < 1000) { return x; }
};

struct Derived : Y {
  int f (int x) override
    post<probe_v>(r: sc::base_contract<X>() && sc::base_contract<Y>())
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
