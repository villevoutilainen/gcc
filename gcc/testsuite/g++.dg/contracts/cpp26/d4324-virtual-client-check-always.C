// D4324: a call names the statically chosen function, which for a virtual
// function may not be the function that runs, so the caller is the only
// place its contract is sure to be checked: a virtual function's contract
// is always checked on the client side.  -fcontracts-client-check applies
// to non-virtual functions only.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontracts-client-check=none" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int client = 0, definition = 0;

struct probe {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void operator() (const sc::assertion_context& ctx) const
  {
    if (ctx.static_info ().side () == sc::assertion_check_side::client)
      ++client;
    else
      ++definition;
    ctx.check ();
  }
};
inline constexpr probe probe_v{};

struct Shape {
  virtual void rotate (double d) pre<probe_v> (d >= 0) {}
  void move (double d) pre<probe_v> (d >= 0) {}
  virtual ~Shape () = default;
};
struct Circle : Shape { void rotate (double) override {} };

int main ()
{
  Circle c;
  Shape& s = c;
  // Names Shape::rotate; Circle::rotate, which runs, has no contract.
  s.rotate (1);
  if (client != 1 || definition != 0)
    __builtin_abort ();
  // Not virtual: checked in its definition only, as the option says.
  s.move (1);
  if (client != 1 || definition != 1)
    __builtin_abort ();
  return 0;
}
