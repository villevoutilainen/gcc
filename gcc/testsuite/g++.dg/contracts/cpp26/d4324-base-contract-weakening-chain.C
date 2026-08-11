// D4324: Eiffel/Ada-style precondition weakening across a 3-level
// linear hierarchy -- Derived's own precondition ORs together
// base_contract<>() for each ancestor, accepting anything any one of
// them would have accepted.
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

struct X {
  virtual int f (int x) pre<probe_v>(x >= 0 && x < 10) { return x; }
  virtual ~X () {}
};
struct Y : X {
  Y () {}
  int f (int x) override pre<probe_v>(x >= 0 && x < 100) { return x; }
};
struct Z : Y {
  Z () {}
  int f (int x) override pre<probe_v>(x >= 0 && x < 1000) { return x; }
};

struct Derived : Z {
  Derived () {}
  int f (int x) override
    pre<probe_v>(sc::base_contract<X>() || sc::base_contract<Y>()
		 || sc::base_contract<Z>())
  { return x * 2; }
};

int main ()
{
  Derived d;
  // 500 fails X's and Y's own range, but satisfies Z's -- accepted via
  // the OR-composition (weakening).
  int r = d.f (500);
  if (r != 1000)
    __builtin_abort ();
  if (calls != 1)
    __builtin_abort ();
  return 0;
}
