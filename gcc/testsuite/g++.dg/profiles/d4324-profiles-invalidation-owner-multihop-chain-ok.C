// P3446R0/P4296R0 Invalidation profile: a fresh owner value's
// obligation follows its VALUE, not any one variable's name -- it can
// pass through any number of plain, unmarked copies before eventually
// being captured by a genuinely [[owner]]-declared variable and
// properly consumed there.  ip_owner_resolve_origin's own multi-hop
// walk (invalidation-profile-gimple.cc) is what makes this legal.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void multihop_chain_ok ()
{
  int *q = new int (42);
  int *r = q;
  [[owner]] int *p = r;
  delete p;
}
