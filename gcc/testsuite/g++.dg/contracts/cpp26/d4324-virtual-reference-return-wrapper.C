// D4324: the caller-side check of a virtual function returning a
// reference, with a postcondition naming the result: the caller's wrapper
// returns the reference the virtual call returns.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int checks = 0;

struct ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void operator() (const sc::assertion_context& ctx) const
  {
    ++checks;
    if (!ctx.check ())
      __builtin_abort ();
  }
};
inline constexpr ctrl ctrl_v{};

struct S {
  int v = 1;
  virtual int& get () post<ctrl_v> (r: r == 1) { return v; }
  virtual ~S () = default;
};

int main ()
{
  S s;
  int& r = s.get ();
  if (&r != &s.v || checks != 2)
    __builtin_abort ();
  return 0;
}
