// D4324: under -fcontract-symbolic-runtime-checks, a relation about the
// members of a returned std::pair ('reachable_from (r.first, r.second)')
// is recorded by the members' values at the call site, and found by a
// precondition checked in another translation unit (the -aux.cc file);
// iterators of two different containers have no such record.
// { dg-do run { target c++26 } }
// { dg-additional-sources "d4324-symbolic-relation-pair-cross-tu-aux.cc" }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks -fcontract-evaluation-semantic=observe" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "d4324-symbolic-relation-pair-cross-tu.h"

int violations;
void handle_contract_violation (const std::contracts::contract_violation &)
{ ++violations; }

std::pair<It, It> whole (std::vector<int>& v) { return {v.begin (), v.end ()}; }

int
main ()
{
  std::vector<int> a{1, 2, 3}, b{4, 5};
  auto r = whole (a);
  if (sum (r.first, r.second) != 6 || violations != 0)
    __builtin_abort ();
  auto [f, l] = whole (b);
  if (sum (f, l) != 9 || violations != 0)
    __builtin_abort ();
  sum (a.begin (), a.begin ());		// no record: a violation
  if (violations != 1)
    __builtin_abort ();
}
