// P4222 Initialization profile, GIMPLE checker: a [[uninit]] local
// whose address is taken anywhere in the function, other than a
// direct write or a recognized [[must_init]] call argument, is not
// verifiable -- an unverifiable [[uninit]] is a hard error, not a
// silently accepted gap (see init-profile-gimple.cc's own top
// comment).  take_ptr's own plain (unflavored) parameter also trips
// the separate call-flavor-consistency check (Phase 3), since an
// [[uninit]]-flavored argument is being passed to it.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void take_ptr (int *);

void f ()
{
  [[uninit]] int x; // { dg-error "cannot verify" }
  take_ptr (&x); // { dg-error "refers to \[^\n\]*memory but its parameter" }
  x = 5;
}
