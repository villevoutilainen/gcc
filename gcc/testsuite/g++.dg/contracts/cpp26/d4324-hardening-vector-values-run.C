// D4324: a violated hardened precondition of the library reports the
// values of its condition's operands: std::vector's subscript out of
// range gives the index.  (The library's own message for it, the
// condition's text again, isn't repeated; 'this' isn't listed.)
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#define _GLIBCXX_ASSERTIONS 1
#define _GLIBCXX_ASSERTIONS_USE_VIOLATION_HANDLER 1
#include <vector>
#include <cstdlib>
#include <cstring>

void
handle_contract_violation (const std::contracts::contract_violation& v)
{
  // The violation terminates after this handler returns, so it ends the
  // test here.
  std::_Exit (std::strcmp (v.comment (), "__n < this->size()") == 0
	      && v.message ()
	      && std::strcmp (v.message (), "10 < this->size()") == 0
	      ? 0 : 1);
}

int
main ()
{
  std::vector<int> v (3);
  return v[10];
}
