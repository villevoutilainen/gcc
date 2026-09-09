// P4222 Initialization profile: curing a whole-object address-escape via
// per-field writes (see the companion d4324-profiles-uninit-aggregate-
// per-field-cure-bad.C) requires EVERY non-artificial field to be
// individually, provably written -- writing only one field of a
// multi-field aggregate must not cure the whole-object escape check,
// since the other field genuinely remains unverified.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct X { int a; int b; };

void other (X* p);

void f ()
{
  X x [[uninit]];
  x.a = 1;
  other (&x); // { dg-error "is not marked" }
  // { dg-error "call to prove it initialized" "" { target *-*-* } .-1 }
}
