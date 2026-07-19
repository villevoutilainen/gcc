// D4324: a review control object logs the violation and returns proceed,
// so execution continues past the failed assertion.  This verifies the
// end-to-end control-object dispatch with a real operator() at runtime.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

bool logged = false;
namespace sc = std::contracts;

struct my_review {
  static constexpr bool is_ignored (sc::evaluation_config) { return false; }
  static constexpr bool constify = false;
  static constexpr bool assumable = false;
  sc::violation_response
  operator() (const char*, std::source_location, sc::evaluation_config) const
  { logged = true; return sc::violation_response::proceed; }
};

int f (int x) pre<my_review>(x > 0) { return x; }

int main ()
{
  int r = f (-1);
  if (!logged)
    __builtin_abort ();
  if (r != -1)
    __builtin_abort ();
  return 0;
}
