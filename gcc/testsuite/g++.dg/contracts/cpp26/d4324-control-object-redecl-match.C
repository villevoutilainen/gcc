// Companion to d4324-control-object-redecl-mismatch.C: the *positive*
// case -- repeating the same control object across a declaration and its
// definition is fine, and so is a bare pre() with no control object
// named at all on either side (which, under -fcontract-control-objects,
// resolves both sides to the same implicit std::contracts::default_v --
// see CONTRACT_CONTROL_OBJECT's own comment in contracts.h -- so this
// isn't merely "no check happened," it's "the check happened and
// matched").
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

void f (int x) pre<std::contracts::review_v>(x >= 0);
void f (int x) pre<std::contracts::review_v>(x >= 0) { }

void g (int x) pre (x >= 0);
void g (int x) pre (x >= 0) { }

int main ()
{
  f (1);
  g (1);
  return 0;
}
