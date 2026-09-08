// P3589: -fprofiles-warning=<profile>,<profile> enables every listed
// profile, not just the first -- this file only violates
// std::invalidation (no std::init violation anywhere), confirming the
// second name in the comma-separated list is genuinely applied too,
// and both are warnings (the compile still succeeds).
// { dg-do compile { target c++11 } }
// { dg-options "-fprofiles-warning=std::init,std::invalidation" }

struct S { int *p; };

void f (S &s)
{
  delete s.p; // { dg-warning "not marked" }
}
