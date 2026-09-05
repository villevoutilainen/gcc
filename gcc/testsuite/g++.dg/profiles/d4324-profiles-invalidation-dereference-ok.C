// P3446R0/P4296R0 Invalidation profile: the blanket "prohibit ALL
// dereferences of pointers" Negative-Baseline placeholder (Phase 7a,
// S7.2's [ub:expr.unary.dereference]) has been removed -- a raw
// pointer parameter with no traceable container binding at all is not
// flagged merely for being dereferenced, unary '*', '->', and array
// subscript alike.  This checker's own mutation-tracking (see
// d4324-profiles-invalidation-raw-pointer-mutation-bad.C and its own
// -ok.C companion) is the real enforcement now: a dereference is
// flagged only when it's provably reached after a mutation of the
// container the pointer was bound to, not unconditionally.  Genuinely
// unprovable non-null safety (Rule #4's own, separate, still-open
// concern) is not attempted here either way.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct S { int m; };

int f1 (int *p)
{
  return *p;
}

int f2 (S *p)
{
  return p->m;
}

int f3 (int *p)
{
  return p[0];
}
