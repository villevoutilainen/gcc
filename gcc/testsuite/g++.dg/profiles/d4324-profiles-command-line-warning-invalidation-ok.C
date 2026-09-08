// P3589: the std::invalidation analogue of d4324-profiles-command-
// line-warning-init-ok.C -- -fprofiles-warning=std::invalidation
// reports a Negative Baseline violation as a warning, and the compile
// still succeeds.
// { dg-do compile { target c++11 } }
// { dg-options "-fprofiles-warning=std::invalidation" }

struct S { int *p; };

void f (S &s)
{
  delete s.p; // { dg-warning "not marked" }
}
