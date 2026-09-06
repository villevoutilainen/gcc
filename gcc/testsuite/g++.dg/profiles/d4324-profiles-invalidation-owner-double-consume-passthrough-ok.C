// P3446R0/P4296R0 Invalidation profile: x is consumed exactly ONCE,
// by the inner call -- the outer call receives g's own (unrelated)
// return value, not x itself, so this is not a double consumption
// despite the nesting.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

int g (int *p [[owner]]);
int f (int);

void caller ([[owner]] int *x)
{
  f (g (x));
}
