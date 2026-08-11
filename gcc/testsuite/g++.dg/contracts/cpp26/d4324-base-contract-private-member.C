// D4324: base_contract<Base>() reaches Base's own precondition even
// when it references a member private to Base -- no friend
// declaration or access grant of any kind is needed in Derived, since
// the predicate is called (through a this-adjusting thunk, built with
// Base's own access), never copied/re-hosted into Derived's context.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int derived_calls = 0;

struct probe {
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

inline constexpr probe probe_v{};

struct Base {
  virtual int f (int x) pre<probe_v>(x >= min_) { return x; }
  virtual ~Base () {}
private:
  int min_ = 0;
};

// No friend declaration, no access grant of any kind.
struct Derived : Base { Derived () {}
  int f (int x) override
    pre<probe_v>(sc::base_contract<Base>())
  { return x * 2; }
};

int main ()
{
  Derived d;
  int r = d.f (5);
  if (r != 10)
    __builtin_abort ();
  if (derived_calls != 1)
    __builtin_abort ();
  return 0;
}
