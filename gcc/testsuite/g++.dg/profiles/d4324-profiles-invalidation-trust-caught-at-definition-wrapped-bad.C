// P3446R0 Invalidation profile: the "wrapped" counterpart of the
// sibling d4324-profiles-invalidation-trust-caught-at-definition-
// direct-bad.C -- same premise, but 'f1' wraps the retained address
// into a small container-like struct instead of returning it bare,
// the same shape push_back/vector{} construction uses throughout this
// file's own worked examples.
//
// 'g1()' binds 'f1's own 'const int&' parameter directly to a named
// local ('f1(local)') -- trusted, no diagnostic at this call site.
// 'f1' itself, however, genuinely retains '&m' into 'X::a' and
// returns it -- exactly the misuse the trust is betting is rare, and
// exactly what gets caught here, at f1's own definition, once f1 is
// compiled under enforce() like any other function.  Compare against
// the sibling d4324-profiles-invalidation-nonempty-return-type-bad.C/
// nontrivial-return-escape-bad.C, which only ever DECLARE 'f1' (an
// opaque, unprovable callee) -- this test supplies a concrete,
// misbehaving DEFINITION to show the other half of that story: the
// trust is not a soundness hole, just a relocation of where the bug
// becomes visible.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct X
{
  const int *a;
};

X f1 (int const &m)
{
  return X{&m}; // { dg-error "may hold a pointer to a local" }
}

X g1 ()
{
  int local = 7;
  return f1 (local);
}
