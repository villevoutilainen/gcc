// P4222 Initialization profile: a member of a local aggregate is cured
// by a whole-object initialization of the aggregate -- both the address-
// escape check (d4324-profiles-aggregate-member-address-escape-bad.C's
// uncured shape) and, once initialized, later reads and field writes.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct X { int a; int b; };

void take_ptr (int *);

int f ()
{
  X x [[uninit]];
  x = {1, 2};		// a whole-object initialization
  x.b = 3;
  take_ptr (&x.b);
  return x.a + x.b;
}
