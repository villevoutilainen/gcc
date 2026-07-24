// D4324: a control object that terminates in its call operator ends the
// program (via std::terminate).  The operator returns void, so termination is
// the control's own responsibility; the program must not reach main's return.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <cstdlib>
#include <exception>

namespace sc = std::contracts;

struct my_terminate_ctrl {
  static constexpr bool is_ignored (sc::evaluation_config) { return false; }
  static constexpr bool constify = false;
  static constexpr bool assumable = false;
  void
  operator() (const sc::assertion_context& ctx) const
  { if (ctx.check ()) return; std::terminate (); }
};

inline constexpr my_terminate_ctrl my_terminate_ctrl_v{};

int f (int x) pre<my_terminate_ctrl_v>(x > 0) { return x; }

void my_terminate ()
{
  _Exit (0);
}

int main ()
{
  std::set_terminate (my_terminate);
  f (-1);
  __builtin_abort ();
}
