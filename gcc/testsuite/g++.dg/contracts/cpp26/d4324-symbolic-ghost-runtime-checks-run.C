// D4324 (P4425): a symbolic contract's predicate is never evaluated at
// run time: with -fcontract-symbolic-runtime-checks, only the record of
// the fact is consulted.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe -fcontract-symbolic-runtime-checks" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

int evaluations, reports;
void handle_contract_violation (const std::contracts::contract_violation &)
{ ++reports; }
struct W {
  int state = 0;
  bool big () const { ++evaluations; return state >= 20; }
  void set (int s) post<std::contracts::symbolic_assert_v> (big ()) { state = s; }
  int use () const pre<std::contracts::symbolic_assert_v> (big ()) { return state; }
};

int main ()
{
  W w;
  w.set (25);
  w.use ();
  W v;
  v.use ();
  /* V's fact was never recorded: reported, observed, still not evaluated.  */
  if (evaluations != 0 || reports != 1)
    __builtin_abort ();
}
