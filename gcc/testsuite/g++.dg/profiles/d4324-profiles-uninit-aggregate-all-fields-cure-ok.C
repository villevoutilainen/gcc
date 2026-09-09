// P4222 Initialization profile: the companion to d4324-profiles-uninit-
// aggregate-partial-field-not-cured-bad.C -- once EVERY field of the
// aggregate is individually, provably written, the whole-object
// address-escape check is cured, exactly as if it had been written as
// one whole-object event.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct X { int a; int b; };

void other (X* p);

void f ()
{
  X x [[uninit]];
  x.a = 1;
  x.b = 2;
  other (&x);
}
