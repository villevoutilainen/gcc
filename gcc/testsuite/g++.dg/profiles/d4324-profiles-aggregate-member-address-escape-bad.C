// P4222 Initialization profile: the member-of-local-aggregate
// counterpart of d4324-profiles-uninit-address-taken-bad.C -- taking
// the address of one field of an [[uninit]] local class-type object
// and handing it to a plain (unflavored) function is unverifiable,
// exactly like the whole-scalar case, and diagnosed the same way:
// anchored at the call that actually takes the address, not at x's
// own declaration.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct X { int a; int b; };

void take_ptr (int *);

int f ()
{
  X x [[uninit]];
  x.a = 1;
  take_ptr (&x.b); // { dg-error "before it is provably initialized" }
  return x.a;
}
