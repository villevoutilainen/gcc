// P4222 Initialization profile: the companion to d4324-profiles-uninit-
// aggregate-partial-field-not-cured-bad.C -- an assignment from a braced
// initializer, which the gimplifier lowers to one store per field, cures
// the whole-object address-escape check like any other whole-object
// initialization.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct X { int a; int b; };

void other (X* p);

void f ()
{
  X x [[uninit]];
  x = {1, 2};		// lowered to per-field stores
  other (&x);
}
