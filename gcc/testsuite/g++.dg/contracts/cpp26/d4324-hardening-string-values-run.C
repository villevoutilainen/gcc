// D4324: a violated hardened precondition of std::string reports the
// index and, as '*this', the string itself, quoted.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#define _GLIBCXX_ASSERTIONS 1
#define _GLIBCXX_ASSERTIONS_USE_VIOLATION_HANDLER 1
#include <string>
#include <cstdlib>
#include <cstring>

void
handle_contract_violation (const std::contracts::contract_violation& v)
{
  // The violation terminates after this handler returns, so it ends the
  // test here.
  std::_Exit (std::strcmp (v.comment (), "__pos <= size()") == 0
	      && v.message ()
	      && std::strcmp (v.message (), "10 <= size() [*this: \"abc\"]") == 0
	      ? 0 : 1);
}

int
main ()
{
  std::string s = "abc";
  return s[10];
}
