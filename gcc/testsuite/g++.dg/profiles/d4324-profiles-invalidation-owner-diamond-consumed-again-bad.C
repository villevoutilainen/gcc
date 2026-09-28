// Sibling of d4324-profiles-invalidation-owner-diamond-read-after-
// delete-bad.C, for leak-point-3 ("consumed again") instead of leak-
// point-4 ("read after consumed"): an unconditional delete right
// after a delete present on only one arm of a branch is a genuine
// double-free on the arm that took the conditional delete, even
// though on the other arm this is a legitimate, first consumption.
// Existential ("may on some path"), not universal ("must on every
// path") -- must be flagged.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f (int *p [[owner]], bool cond)
{
  if (cond)
    delete p;
  delete p; // { dg-error "consumed again here, after possibly already being consumed" }
}
