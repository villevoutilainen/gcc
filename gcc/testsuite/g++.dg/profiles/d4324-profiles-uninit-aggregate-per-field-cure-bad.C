// P4222 Initialization profile: a local aggregate whose every field is
// individually, provably written (e.g. 'x = { 42 };', which GCC's own
// gimplifier decomposes into a per-field store 'x.y = 42;' before this
// checker ever sees a single whole-object assignment) is cured for the
// whole-object address-escape check AND flavor-consistency, exactly as
// if it had been written as one whole-object event -- not just for the
// one field's own reads/escapes (already handled separately by
// d4324-profiles-uninit-member-cured-by-write-ok.C). Godbolt report:
// godbolt.org/z/6sEfshdr5.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct X { int y; };

void f (X* p [[ref_to_uninit]]);
void g (X* p);

void tst ()
{
  X x [[uninit]];
  X y = {7};
  f (&x);
  g (&x); // { dg-error "is not marked" }
  // { dg-error "before it is provably initialized" "" { target *-*-* } .-1 }
  f (&y); // { dg-error "must refer to" }
  g (&y);
  x = { 42 };
  g (&x);
}
