// P3446R0/P4296R0 Invalidation profile: handing x off to another
// owner-marked local y, then consuming y exactly once and never
// touching x again under its own name, is a perfectly ordinary,
// single hand-off chain -- not a leak, not a double-consumption.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

int f (int *p [[owner]]);

void caller ([[owner]] int *x)
{
  [[owner]] int *y = x;
  f (y);
}
