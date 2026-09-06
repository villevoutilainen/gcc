// P3446R0/P4296R0 Invalidation profile: a single consuming call
// wrapped arbitrarily deep inside other, unrelated calls is still
// just ONE consumption -- deep nesting around the ONE call that
// actually touches x is not itself a hazard.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

int f (int *p [[owner]]);
int wrap1 (int);
int wrap2 (int);
int wrap3 (int);

void caller ([[owner]] int *x)
{
  wrap3 (wrap2 (wrap1 (f (x))));
}
