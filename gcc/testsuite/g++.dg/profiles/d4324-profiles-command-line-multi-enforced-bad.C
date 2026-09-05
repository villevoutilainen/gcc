// P3589: -fprofiles-enforced=<profile>,<profile> enforces every
// listed profile, not just the first -- this file only violates
// std::invalidation (no std::init violation anywhere), confirming the
// second name in the comma-separated list is genuinely applied too.
// { dg-do compile { target c++11 } }
// { dg-options "-fprofiles-enforced=std::init,std::invalidation" }

struct S { int *p; };

void f (S &s)
{
  delete s.p; // { dg-error "not marked" }
}
