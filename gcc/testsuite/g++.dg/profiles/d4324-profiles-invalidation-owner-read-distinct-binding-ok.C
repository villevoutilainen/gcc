// P3446R0/P4296R0 Invalidation profile: consuming one [[owner]]
// binding must not affect reads of a completely different one -- each
// tracked binding's own dataflow is computed and checked
// independently.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f ([[owner]] int *p, [[owner]] int *q)
{
  delete p;
  int x = *q;
  (void) x;
  delete q;
}
