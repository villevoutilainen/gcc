// P3446R0/P4296R0 Invalidation profile: same known-alias tracking as
// d4324-profiles-invalidation-owner-alias-double-consume-bad.C, but
// through two ordinary consuming calls (not two deletes) -- x is
// handed off to y, y is consumed by f, and the later call to g still
// correctly sees x itself as already spent.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

int f (int *p [[owner]]);
int g (int *p [[owner]]);

void caller ([[owner]] int *x)
{
  [[owner]] int *y = x;
  f (y);
  g (x); // { dg-error "consumed again here" }
}
