// P3446R0/P4296R0 Invalidation profile: reading an [[owner]] pointer
// after a delete present on only ONE arm of a branch -- the identical
// universal-vs-existential soundness bug Rule #0/#1's own mutation
// tracking already had and was fixed for (see d4324-profiles-
// invalidation-diamond-single-arm-mutation-bad.C), found again in the
// separate owner-consumption checker's own leak-point-4 (read-after-
// consumed) diagnostic. The conditional delete does not dominate the
// merge point, but on the true-branch execution the dereference below
// genuinely reads through an already-deleted pointer -- silence here
// would mean "not provably unsafe", not "provably safe", which this
// profile must never accept.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f (int x)
{
  int *p [[owner]] = new int{9}; // { dg-error "never deleted or passed on" }
  if (0 < x)
    delete p;
  *p = 10; // { dg-error "read here, after possibly already being consumed" }
}
