// Negative control for the diamond-shaped leak-point-3/4 fix: every
// path through this if/else consumes p exactly once, so nothing here
// is unsafe on any execution -- must stay clean, confirming the
// existential fix doesn't over-fire just because a merge point is
// reached by more than one consuming statement.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f (int *p [[owner]], bool cond)
{
  if (cond)
    delete p;
  else
    delete p;
}
