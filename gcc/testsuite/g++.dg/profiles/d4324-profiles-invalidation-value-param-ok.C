// Negative control for d4324-profiles-invalidation-addr-of-value-param-
// bad.C and d4324-profiles-invalidation-string-view-of-value-param-
// bad.C: confirms neither fix over-widened what counts as "local".
// - f: an ordinary pointer parameter's own VALUE flowing out never
//   dangles, regardless of what it points to -- must stay the existing,
//   correct "parameter's own default-def, never &local" behavior.
// - g: a genuine reference parameter's own address is the caller's
//   object, not this function's -- DECL_BY_REFERENCE is deliberately
//   what distinguishes this from the ABI-promoted-by-value case, not
//   REFERENCE_TYPE alone.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

int *
f (int *p)
{
  return p;
}

int *
g (int &r)
{
  return &r;
}
