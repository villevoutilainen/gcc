// P3446R0/P4296R0 Invalidation profile, Phase 7a Negative Baseline: a
// reinterpret_cast whose *target* type is not a pointer (e.g. to an
// integer, to inspect a pointer's bit pattern) is not what this rule
// is aimed at -- it cannot itself be used to reuse storage as a
// different type -- so it is not flagged.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

typedef __UINTPTR_TYPE__ uintptr_t;

uintptr_t f (int *p)
{
  return reinterpret_cast<uintptr_t> (p);
}
