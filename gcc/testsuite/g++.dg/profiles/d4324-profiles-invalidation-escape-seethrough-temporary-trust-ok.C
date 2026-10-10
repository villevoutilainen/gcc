// P3446R0 Invalidation profile: a deliberate, explicit trust decision
// (confirmed with the user), not a soundness proof -- recorded
// distinctly from the sibling escape-seethrough-temp-ok.C (which IS a
// full proof, tracing what a temp holds back to something already
// known-safe). Here, 'f1' is a bare declaration -- its own body is
// completely invisible, so there is no way to prove it doesn't retain
// '&m' (the address of the temporary materialized to bind its own
// 'const int&' parameter) rather than merely reading through it once.
//
// The trust: it should be rare for a function taking a reference-to-
// const/rvalue-reference parameter to incorrectly retain that
// reference (or its address) instead of just reading through it: and
// if it does, and f1 itself is ALSO compiled under the invalidation
// profile, f1's OWN return-escape check would independently catch it
// there (e.g. via the same direct-parameter-value check that already
// catches 'const int* g(const int& x) { return &x; }'). This applies
// uniformly to any callee bound this way, not just well-known standard
// library operations. This trust has since been widened to also cover
// a genuinely-named decl's own address taken directly to bind a
// reference parameter, not just a materialized temporary -- see
// d4324-profiles-invalidation-escape-seethrough-named-arg-ok.C --
// since a reference carries no runtime distinction between the two.
// What stays excluded: an explicit, computed '&decl' passed to an
// ORDINARY, non-reference parameter, and any argument that is not
// itself a direct, top-level '&decl' at the call site (e.g. a
// reference read directly, tracing back to something fragile) -- see
// d4324-profiles-invalidation-escape-explicit-address-still-bad.C and
// d4324-profiles-invalidation-nonempty-return-type-bad.C.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct X
{
  int *a;
};

X f1 (int const &m);

X g1 ()
{
  return f1 (7);
}
