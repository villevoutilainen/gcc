// P3446R0/P4296R0 Invalidation profile: the same double-consumption
// as d4324-profiles-invalidation-owner-double-consume-bad.C, but with
// the call's own arguments split across multiple source lines -- the
// check must still catch the second consuming argument regardless of
// how the enclosing call is formatted.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

int f (int *p [[owner]]);
int g (int *p [[owner]]);
void h (int, int);

void caller ([[owner]] int *x)
{
  h (f (x), // { dg-error "consumed again here" }
     g (x));
}
