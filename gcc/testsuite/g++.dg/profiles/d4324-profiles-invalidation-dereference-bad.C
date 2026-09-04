// P3446R0/P4296R0 Invalidation profile, Phase 7a Negative Baseline
// (S7.2, [ub:expr.unary.dereference]): "we'll be prohibiting ALL the
// dereferences of pointers" -- unary '*', '->', and array subscript
// on a pointer are all rejected once the std::invalidation profile
// is enforced (Rule #4, provable-non-null, is not yet implemented --
// see Phase 7b).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct S { int m; };

int f1 (int *p)
{
  return *p; // { dg-error "dereference of a pointer not permitted" }
}

int f2 (S *p)
{
  return p->m; // { dg-error "dereference of a pointer not permitted" }
}

int f3 (int *p)
{
  return p[0]; // { dg-error "dereference of a pointer not permitted" }
}
