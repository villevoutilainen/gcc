// D4324: a contract naming a control object is checked through a
// compiler-made 'bool thunk (void *p)' that unpacks the predicate's
// arguments from P.  When the predicate has none to unpack, the thunk never
// reads P, which mustn't draw -Wunused-parameter: the parameter is the
// compiler's.  (Seen with libc++'s hardening assertions under -Werror, its
// headers not being system headers in its own tests.)
// { dg-do compile { target c++26 } }
// { dg-options "-fcontracts -fcontract-control-objects -Wunused-parameter" }

#include <contracts>

constexpr int
f ()
{
  contract_assert<std::contracts::noexcept_assert_v> (true); // { dg-bogus "unused parameter" }
  return 0;
}

int
g (int x)
{
  contract_assert<std::contracts::noexcept_assert_v> (x > 0); // { dg-bogus "unused parameter" }
  return x;
}

int
h (int x)
  pre<std::contracts::noexcept_assert_v> (x > 0) // { dg-bogus "unused parameter" }
{
  return x;
}

int
main ()
{
  return f () + g (1) + h (1) - 2;
}
