// P3446R0/P4296R0 Invalidation profile: reading an [[owner]] pointer
// BEFORE it is consumed is the ordinary, common case and must stay
// accepted -- the new read-after-consumption check only applies once
// the value has genuinely already been spent.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f ([[owner]] int *p)
{
  int x = *p;
  delete p;
  (void) x;
}
