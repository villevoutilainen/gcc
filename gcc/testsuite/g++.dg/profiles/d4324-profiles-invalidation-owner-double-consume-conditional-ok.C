// P3446R0/P4296R0 Invalidation profile: the double-consumption check
// must NOT fire when the two consuming calls are mutually exclusive
// -- exactly one of f (x)/g (x) ever executes on any given run, so x
// is consumed exactly once on every path.  Distinguishes a sound
// "provably consumed on every path reaching here" check from an
// unsound one that would flag any two syntactically-similar consuming
// calls regardless of control flow.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

int f (int *p [[owner]]);
int g (int *p [[owner]]);

int caller ([[owner]] int *x, bool c)
{
  if (c)
    return f (x);
  return g (x);
}
