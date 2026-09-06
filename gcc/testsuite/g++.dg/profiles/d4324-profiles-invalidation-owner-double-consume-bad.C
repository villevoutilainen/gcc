// P3446R0/P4296R0 Invalidation profile: an [[owner]] value reaching
// TWO separate consuming events (here, two separate calls, each
// taking it by an owner-accepting parameter) on the same straight-
// line path is a double-free waiting to happen -- the second call
// receives a value the first one already deleted (or otherwise
// transferred away).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

int f (int *p [[owner]]);
int g (int *p [[owner]]);
void h (int, int);

void caller ([[owner]] int *x)
{
  h (f (x), g (x)); // { dg-error "consumed again here" }
}
