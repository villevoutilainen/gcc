// P3446R0/P4296R0 Invalidation profile: a fresh 'new T' allocation is,
// by construction, an owning value -- the caller holds the only
// pointer to it -- so it's recognized as owner-flavored even though
// operator new itself is never [[owner]]-marked. Passing it directly
// to an owner-accepting parameter, or capturing it into an
// [[owner]]-marked local, is not a flavor mismatch.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void sink (int *p [[owner]]) { delete p; }

void direct_ok ()
{
  sink (new int);
}

void via_local_ok ()
{
  [[owner]] int *p = new int;
  delete p;
}
