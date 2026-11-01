// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -D_GLIBCXX_CONVEYOR_ASSERTIONS" }
#include "d4324-template-contract-3.H"

const char *
g (const std::string &s)
  pre<std::contracts::conveyor_assert_v> (std::is_object_address (&s))
{
  return s.c_str ();
}

int
main ()
{
  std::string s ("x");
  return g (s)[0] != 'x';
}
