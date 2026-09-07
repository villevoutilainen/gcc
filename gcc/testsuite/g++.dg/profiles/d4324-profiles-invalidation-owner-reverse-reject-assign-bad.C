// P3446R0/P4296R0 Invalidation profile: assigning an arbitrary,
// untracked raw pointer into an explicitly [[owner]]-declared local
// is still rejected -- unlike the forward (owner-into-non-owner)
// direction, this reverse direction stays enforced, since the callee
// (here, this same function's own later 'delete p;') will eventually
// try to delete something this checker never saw genuinely allocated.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void reverse_reject_assign_bad (int *arbitrary)
{
  [[owner]] int *p = arbitrary; // { dg-error "assigning a pointer not marked" }
  delete p;
}
