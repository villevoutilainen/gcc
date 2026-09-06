// P3446R0/P4296R0 Invalidation profile: same double-consumption
// hazard as the sibling tests here, but with BOTH consuming calls
// nested one level deep (as opposed to one flat and one nested) --
// confirms nesting depth is irrelevant on either side, not just one.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

int f (int *p [[owner]]);
int g (int *p [[owner]]);
int mid1 (int);
int mid2 (int);
void outer (int, int);

void caller ([[owner]] int *x)
{
  outer (mid1 (f (x)), mid2 (g (x))); // { dg-error "consumed again here" }
}
