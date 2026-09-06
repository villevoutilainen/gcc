// P3446R0/P4296R0 Invalidation profile: an [[owner]] parameter deleted
// on only ONE branch of an if/else still leaks on the other path.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f ([[owner]] int *p, bool c) // { dg-error "never deleted or passed on" }
{
  if (c)
    delete p;
}
