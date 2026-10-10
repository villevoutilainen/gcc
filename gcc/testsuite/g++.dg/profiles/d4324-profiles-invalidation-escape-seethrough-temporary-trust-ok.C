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
// library operations -- see d4324-profiles-invalidation-nonempty-
// return-type-bad.C/nontrivial-return-escape-bad.C's own comments for
// the deliberately-NOT-trusted contrast: passing an already-named,
// independently-long-lived local or parameter (not a temporary) gets
// none of this trust at all, and stays exactly as conservative as ever.
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
