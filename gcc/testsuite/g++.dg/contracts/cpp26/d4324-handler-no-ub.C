// D4324: a control object whose operator() returns terminate causes
// contract-termination (via std::terminate) after the handler returns.
// The program must not reach main's return statement.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <cstdlib>
#include <exception>

namespace sc = std::contracts;

struct my_terminate_ctrl {
  static constexpr bool is_ignored (sc::evaluation_config) { return false; }
  static constexpr bool constify = false;
  static constexpr bool assumable = false;
  sc::violation_response
  operator() (const char*, std::source_location, sc::evaluation_config) const
  { return sc::violation_response::terminate; }
};

int f (int x) pre<my_terminate_ctrl>(x > 0) { return x; }

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
