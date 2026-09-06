// P3446R0/P4296R0 Invalidation profile: same hazard as
// d4324-profiles-invalidation-owner-double-consume-bad.C, but the two
// consuming calls sit at DIFFERENT syntactic nesting depths --
// f (x) directly, g (x) one level deeper inside i (...). Demonstrates
// that detection is driven purely by GIMPLE statement order, not by
// AST nesting shape: by the time this checker runs, gimplification
// has already flattened both call trees into a straight-line sequence
// of statements regardless of how deeply either was originally
// nested, so which side is "more nested" makes no difference to
// whether the double consumption is caught.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

int f (int *p [[owner]]);
int g (int *p [[owner]]);
int i (int);
void h (int, int);

void caller ([[owner]] int *x)
{
  h (f (x), i (g (x))); // { dg-error "consumed again here" }
}
