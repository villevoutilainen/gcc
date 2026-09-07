// P3446R0/P4296R0 Invalidation profile: a plain assignment of an
// owner-flavored pointer into an unmarked one is an ordinary,
// harmless "peek" -- the destination isn't claiming ownership, so
// there's nothing to be inconsistent about, and it does NOT start a
// second, independent tracked binding of its own (a design correction
// after an earlier version of this checker treated it as both a
// flavor mismatch AND a fresh binding needing its own consumption --
// see ip_owner_resolve_origin's own comment, invalidation-profile-
// gimple.cc). p alone remains responsible, and is properly consumed.
// This is the user's own original repro for this fix.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f ([[owner]] int *p)
{
  int *q = p;
  (void) q;
  delete p;
}
