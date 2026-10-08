// P3446R0/P4296R0 Invalidation profile: a genuine false negative this
// checker used to have. 'push_back' is an ordinary (non-const, non-
// constructor) member call -- before this fix, the "what established
// this container's contents" search only recognized a constructor call
// or a plain assignment as a write to the container, so 'push_back'
// was invisible to it entirely. The search would walk straight past
// the push and land on the container's own (empty, safe) construction
// instead, concluding "safe" without ever having looked at what was
// actually pushed. This is not a precision gap (an argument whose
// safety couldn't be determined, conservatively flagged anyway) --
// the whole statement was simply never visited, so a genuinely
// escaping local address slipped through silently. Fixed by teaching
// that same search to also recognize an ordinary mutating member call
// as establishing a container's contents (the same "assume a non-
// const call mutates its receiver" rule Rule #0/#1 already applies
// elsewhere in this file), so push_back's own argument is finally
// inspected.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

std::vector<int*> f ()
{
  std::vector<int*> v;
  int local = 0;
  v.push_back (&local);
  return v; // { dg-error "may hold a pointer to a local" }
}
