// P3446R0/P4296R0 Invalidation profile, Phase 7a Negative Baseline
// (S7.2, [ub:basic.stc.alloc.dealloc.constraint]): a user-defined
// operator new/delete (member or global-scope) is rejected once the
// std::invalidation profile is enforced.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

typedef __SIZE_TYPE__ size_t;

void *operator new (size_t sz) // { dg-error "user-defined" }
{
  return (void *) 0;
}

struct S
{
  void *operator new (size_t sz); // { dg-error "user-defined" }
};
