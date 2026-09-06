// P3446R0/P4296R0 Invalidation profile, flavor-consistency layer: a
// plain assignment between two ordinary, unmarked pointers -- baseline,
// no attributes involved at all.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f (int *p)
{
  int *q = p;
  (void) q;
}
