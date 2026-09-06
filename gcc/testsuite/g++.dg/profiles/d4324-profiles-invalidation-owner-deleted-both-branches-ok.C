// P3446R0/P4296R0 Invalidation profile: an [[owner]] parameter deleted
// on EVERY path (the textbook diamond merge) is fully consumed --
// exercises the forward "may still be owned" dataflow's own
// OR-across-predecessors fixed point getting the merge point right
// (see ip_compute_owner_reach_info's own comment,
// invalidation-profile-gimple.cc).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f ([[owner]] int *p, bool c)
{
  if (c)
    delete p;
  else
    delete p;
}
