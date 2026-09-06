// P3446R0/P4296R0 Invalidation profile: calling a function that
// returns an [[owner]] pointer and dropping the result is a leak --
// the SECOND trigger case (as opposed to receiving an [[owner]]
// parameter directly).  Also trips the flavor-consistency layer,
// since the result is captured into an unmarked pointer.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

[[owner]] int* make ();

void f ()
{
  int *p = make (); // { dg-error "assigning a pointer marked" }
  // { dg-error "never deleted or passed on" "" { target *-*-* } .-1 }
}
