// P3446R0 Invalidation profile: demonstrates the premise behind the
// reference-binding trust is not just assumed -- it's verifiable.
// 'h()' binds 'g's own 'const int&' parameter directly to a named
// local ('g(local)') -- exactly the shape the trust in
// ip_call_argument_escapes_locally_p now extends to, so this call
// site produces NO diagnostic at all, same as if 'g' had been called
// with a literal.
//
// The trust holds specifically because 'g' misbehaving (retaining
// '&x' instead of merely reading through it) is NOT silently missed
// elsewhere: 'g' is itself compiled under enforce() right here, and
// its own, completely independent return-escape check catches the
// exact misuse this trust is betting won't happen -- the diagnostic
// simply relocates from every call site to the one place that can
// actually see the misuse, 'g's own definition.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

const int *g (const int &x)
{
  return &x; // { dg-error "may hold a pointer to a local" }
}

const int *h ()
{
  int local = 7;
  return g (local);
}
