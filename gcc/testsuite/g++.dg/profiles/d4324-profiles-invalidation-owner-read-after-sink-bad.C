// P3446R0/P4296R0 Invalidation profile: reading an [[owner]] pointer
// after handing it to another owner-accepting function -- passing an
// owner value doesn't merely make it "no longer owned here", it
// hands away this checker's own visibility into whatever the callee
// does with it, including destroying the object; a later read is
// unsafe on that same reasoning as a read-after-delete.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void sink (int *q [[owner]]);

void f ([[owner]] int *p)
{
  sink (p);
  int x = *p; // { dg-error "is read here, after already being consumed" }
  (void) x;
}
