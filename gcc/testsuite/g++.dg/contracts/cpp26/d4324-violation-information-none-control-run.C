// D4324: -fcontract-violation-information=none empties every assertion's
// comment and source location -- here a control object (default_v, and the bare pre that names it).  The handler sees an empty
// comment, empty file and function names and line 0, and the predicate
// text isn't in the program.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe -fcontract-violation-information=none -fdump-tree-gimple" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <cstring>

int calls;
bool empty_ok;

void
handle_contract_violation (const std::contracts::contract_violation& v)
{
  ++calls;
  empty_ok = (std::strcmp (v.comment (), "") == 0
	      && std::strcmp (v.location ().file_name (), "") == 0
	      && std::strcmp (v.location ().function_name (), "") == 0
	      && v.location ().line () == 0);
}

int f (int x) pre<std::contracts::default_v>(x > 0) { return x; }
int g (int x) { contract_assert (x > 1); return x; }

int main ()
{
  f (0);
  if (calls != 1 || !empty_ok)
    __builtin_abort ();
  g (0);
  if (calls != 2 || !empty_ok)
    __builtin_abort ();
}

// { dg-final { scan-tree-dump-not "\"x > 0\"" "gimple" } }
// { dg-final { scan-tree-dump-not "\"x > 1\"" "gimple" } }
