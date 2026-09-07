// P3446R0/P4296R0 Invalidation profile: passing a fresh owner value
// through any number of plain, unmarked copies is legal (see the
// companion -ok test), but the underlying obligation still must
// eventually be discharged through a genuinely owner-declared name --
// q's own tracked binding (established directly by the 'new'
// -expression) is never consumed here at all, through any name.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void multihop_leak_bad ()
{
  int *q = new int (42); // { dg-error "never deleted or passed on" }
  int *r = q;
  (void) r;
}
