// P3446R0/P4296R0 Invalidation profile: THREE separate consuming
// calls sharing the same owner value, not just two -- each consuming
// event after the first is independently flagged.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

int f (int *p [[owner]]);
int g (int *p [[owner]]);
int k (int *p [[owner]]);

void caller ([[owner]] int *x)
{
  f (x);
  g (x); // { dg-error "consumed again here" }
  k (x); // { dg-error "consumed again here" }
}
