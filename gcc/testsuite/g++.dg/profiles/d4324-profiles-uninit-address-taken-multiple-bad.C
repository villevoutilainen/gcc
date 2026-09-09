// P4222 Initialization profile, GIMPLE checker: taking the address of
// the same [[uninit]] local unverifiably more than once in a single
// function produces one diagnostic per occurrence, not just the
// first -- each occurrence is its own independent, individually
// annotatable/suppressible problem.  Companion to d4324-profiles-
// uninit-address-taken-bad.C, which covers the single-occurrence
// case.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void take_ptr (int *);

void f ()
{
  [[uninit]] int x;
  take_ptr (&x); // { dg-error "refers to \[^\n\]*memory but its parameter" }
  // { dg-error "call to prove it initialized" "" { target *-*-* } .-1 }
  take_ptr (&x); // { dg-error "refers to \[^\n\]*memory but its parameter" }
  // { dg-error "call to prove it initialized" "" { target *-*-* } .-1 }
  x = 5;
}
