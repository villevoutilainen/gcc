// P3446R0/P4296R0 Invalidation profile: std::as_owner() only lifts
// the Negative-Baseline restriction when it is itself the delete-
// expression's own operand -- an ordinary delete of a plain, non-
// owner-marked pointer, with no std::as_owner() anywhere, is still
// flagged exactly as before.  Confirms as_owner is doing real,
// deliberate work rather than disabling the check altogether.
// { dg-do compile { target c++11 } }
// { dg-options "-std=c++20" }

[[profiles::enforce(std::invalidation)]];

void f (int *p)
{
  delete p; // { dg-error "not marked" }
}
