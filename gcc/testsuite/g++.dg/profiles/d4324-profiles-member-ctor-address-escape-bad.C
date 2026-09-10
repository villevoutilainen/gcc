// P4222 Initialization profile: the constructor-member counterpart of
// d4324-profiles-uninit-address-taken-bad.C -- taking the address of
// an [[uninit]] non-static data member inside its own constructor and
// handing it to a plain (unflavored) function is unverifiable, exactly
// like the local-scalar case, and diagnosed the same way: anchored at
// the call that actually takes the address, not at the enclosing
// constructor's own declaration (the previous, even less useful,
// anchor point for this specific diagnostic).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void take_ptr (int *);

struct X
{
  [[uninit]] int m;
  X () { take_ptr (&m); } // { dg-error "before it is provably initialized" }
  // { dg-error "refers to \[^\n\]*memory but its parameter" "" { target *-*-* } .-1 }
};
