// P4222 Initialization profile: a member of a local aggregate is cured
// by a direct write to that field exactly like a plain scalar local is
// (see d4324-profiles-uninit-cured-by-write-ok.C) -- both the address-
// escape check (d4324-profiles-aggregate-member-address-escape-bad.C's
// uncured shape) and, once initialized, later reads.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct X { int a; int b; };

void take_ptr (int *);

int f ()
{
  X x [[uninit]];
  x.a = 1;
  x.b = 2;
  take_ptr (&x.b);
  return x.a + x.b;
}
