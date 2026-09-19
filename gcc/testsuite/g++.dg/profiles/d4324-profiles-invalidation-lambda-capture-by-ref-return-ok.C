// Companion to d4324-profiles-invalidation-lambda-capture-by-ref-return-
// bad.C: the same shape, but capturing by value instead of by reference,
// so nothing about the returned closure can dangle. Confirms the
// completeness-deferral fix for that test's own ICE doesn't introduce any
// new false positive for an otherwise-safe lambda.
// { dg-do compile { target c++14 } }

[[profiles::enforce(std::invalidation)]];

auto f ()
{
  int x = 4;
  return [=] (int y) mutable { return x = y; };
}

void g ()
{
  auto lam = f ();
  int z = lam (7);
  (void) z;
}
