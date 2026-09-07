// P3446R0/P4296R0 Invalidation profile: a read reached only via the
// branch that did NOT consume the value must stay accepted -- the
// new read-after-consumption check is path-sensitive, exactly like
// the existing double-consumption check it shares its dataflow with.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f (bool c, [[owner]] int *p)
{
  if (c)
    delete p;
  else
    {
      int x = *p;
      (void) x;
      delete p;
    }
}
