// D4324: default_v's operator() isn't noexcept, so an exception thrown by its
// predicate propagates to the caller: no termination, no violation report.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

struct oops {};

bool
throwing_pred (int)
{
  throw oops {};
}

int f (int x) pre<std::contracts::default_v>(throwing_pred (x)) { return x; }
int g (int x) pre (throwing_pred (x)) { return x; }	// bare pre: default_v

int main ()
{
  int caught = 0;
  try { f (1); } catch (const oops&) { ++caught; }
  try { g (1); } catch (const oops&) { ++caught; }
  return caught == 2 ? 0 : 1;
}
