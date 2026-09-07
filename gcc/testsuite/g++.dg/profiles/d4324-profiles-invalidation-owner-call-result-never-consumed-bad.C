// P3446R0/P4296R0 Invalidation profile: calling a function that
// returns an [[owner]] pointer and dropping the result is a leak --
// the SECOND trigger case (as opposed to receiving an [[owner]]
// parameter directly).  Capturing a FRESH owner-flavored result (a
// 'new'-expression or an owner-returning call, as here) still starts
// a tracked binding regardless of the destination's own [[owner]]
// marking -- unlike a plain copy of an ALREADY-tracked value, which
// does not (see ip_arg_owner_flavored_p_1's own comment).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

[[owner]] int* make ();

void f ()
{
  int *p = make (); // { dg-error "never deleted or passed on" }
}
