// P4222 Initialization profile: std::now_init_in_place()'s actual
// motivating case -- an [[uninit]] local's address is handed to an
// opaque function through an intermediate pointer (a shape this
// checker's local, single-hop, non-aliasing analysis can never trace,
// and is not attempting to: general pointer-aliasing analysis isn't
// solvable in general).  now_init_in_place() attaches its assertion
// directly to the object's own name, so a DIRECT read of the object
// afterward (not through the pointer used to reach it) is accepted --
// proving the assertion is about the object, not about whichever
// pointer happened to reach it.  Companion
// d4324-profiles-now-init-in-place-escaped-bad.C is the identical
// shape without the assertion, still rejected.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];
#include <utility>

void write_somehow (void *p [[ref_to_uninit]]);

int use_it ()
{
  int x [[uninit]];
  int *p [[ref_to_uninit]] = &x;
  write_somehow (p);
  std::now_init_in_place (x);
  return x;
}
