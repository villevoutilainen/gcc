// D4324: compiler builtins (__builtin_unreachable, __builtin_expect)
// are exempt from the callee-must-be-conveyor rule -- their behavior is
// fully known to the compiler, the same trust the array-bounds/div-mod
// scanners already extend to primitive operators, and a conveyor
// function may legitimately need one (e.g. size()'s own real-world use
// of __builtin_unreachable() as a negative-result guard). std::
// unreachable() (the library wrapper, not the raw builtin) is still
// separately, specifically rejected by check_conveyor_function_body_r,
// confirmed here to pin the asymmetry deliberately -- see contracts.cc's
// own is_std_unreachable_fndecl_p comment. See .claude/plans/well-we-
// last-discussed-ethereal-duckling.md.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <utility>

int f (int x) conveyor
{
  if (__builtin_expect (x > 0, 1))
    return x;
  __builtin_unreachable ();
}

int g (int x) conveyor
{
  if (x > 0)
    return x;
  std::unreachable (); // { dg-error "std::unreachable. not permitted in a conveyor function or predicate" }
}

int main () { return f (1) + g (1) - 2; }
