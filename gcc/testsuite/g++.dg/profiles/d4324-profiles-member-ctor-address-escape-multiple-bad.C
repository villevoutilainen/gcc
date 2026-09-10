// P4222 Initialization profile: the constructor-member counterpart of
// d4324-profiles-uninit-address-taken-multiple-bad.C -- taking the
// address of the same [[uninit]] member unverifiably more than once
// inside its own constructor produces one diagnostic per occurrence,
// not just the first.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void take_ptr (int *);

struct X
{
  [[uninit]] int m;
  X ()
  {
    take_ptr (&m); // { dg-error "refers to \[^\n\]*memory but its parameter" }
    // { dg-error "before it is provably initialized" "" { target *-*-* } .-1 }
    take_ptr (&m); // { dg-error "refers to \[^\n\]*memory but its parameter" }
    // { dg-error "before it is provably initialized" "" { target *-*-* } .-1 }
  }
};
