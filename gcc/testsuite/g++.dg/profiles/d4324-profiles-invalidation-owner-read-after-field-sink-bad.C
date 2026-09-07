// P3446R0/P4296R0 Invalidation profile: reading an [[owner]] pointer
// after storing it into an [[owner]]-marked field -- ownership (and
// this checker's own visibility into the object's fate) has passed
// to the containing object; a later read of the original name is
// unsafe on the same reasoning as every other consuming event.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct S { [[owner]] int *m; };

void f ([[owner]] int *p, S &s)
{
  s.m = p;
  int x = *p; // { dg-error "is read here, after already being consumed" }
  (void) x;
}
