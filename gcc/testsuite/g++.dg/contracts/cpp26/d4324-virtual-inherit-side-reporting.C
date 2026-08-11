// D4324: an inherited check's side() must report the actual calling
// context it runs in -- client when reached via Derived::f's own
// caller-side wrapper, definition when reached from Derived::f's own
// body -- never simply replaying Base::f's own perspective.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int client_calls = 0;
int definition_calls = 0;

struct base_probe {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool inherited (sc::assertion_static_info) { return true; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    if (ctx.static_info ().side () == sc::assertion_check_side::client)
      client_calls++;
    else if (ctx.static_info ().side () == sc::assertion_check_side::definition)
      definition_calls++;
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
  Derived d;
  int r = d.f (5);
  if (r != 10)
    __builtin_abort ();

  // One direct call to Derived::f: the statically-named call triggers
  // both the caller-side wrapper (client) and Derived::f's own body
  // (definition) -- each reporting the context it actually ran in.
  if (client_calls != 1 || definition_calls != 1)
    __builtin_abort ();

  return 0;
}
