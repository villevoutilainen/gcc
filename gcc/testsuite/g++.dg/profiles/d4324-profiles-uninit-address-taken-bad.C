// P4222 Initialization profile, GIMPLE checker: a [[uninit]] local
// whose address is taken anywhere in the function is not SSA-
// registered, so this checker cannot analyze it at all -- an
// unverifiable [[uninit]] is a hard error, not a silently accepted
// gap (see init-profile-gimple.cc's own top comment).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void take_ptr (int *);

void f ()
{
  [[uninit]] int x; // { dg-error "cannot verify" }
  take_ptr (&x);
  x = 5;
}
