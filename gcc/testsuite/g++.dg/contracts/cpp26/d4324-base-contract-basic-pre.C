// D4324: std::contracts::base_contract<Base>() -- an explicit,
// user-written reference to a named base class's own precondition-set,
// combined with an ordinary boolean expression. Confirms the reference
// evaluates just the bare predicate (never dispatching through Base's
// own control object) and that Derived's own contract's own dispatch
// is what actually runs.
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
  int f (int x) override
    pre<derived_probe_v>(sc::base_contract<Base>() && x < 100)
  { return x * 2; }
};

int main ()
{
  Derived d;
  int r = d.f (5);
  if (r != 10)
    __builtin_abort ();

  // Base's own control object never dispatched -- only its bare
  // predicate was evaluated, as part of Derived's own contract check.
  if (base_calls != 0)
    __builtin_abort ();
  if (derived_calls != 1)
    __builtin_abort ();

  return 0;
}
