// P4222 Initialization profile: std::now_init_in_place(x) is an
// ordinary [[must_init]] call (IE-2) on x itself, so it cures x for
// every purpose exactly like a plain write does (see the companion
// d4324-profiles-uninit-cured-by-write-ok.C) -- not just for a later
// plain read, which std::now_init_in_place already handled before this
// fix.  Godbolt report: a first, unverified g(&x) before the assertion
// is still (correctly) flagged; a second g(&x) after it is not.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];
#include <utility>

struct X { int y; };

void f (X *p [[ref_to_uninit]]);
void g (X *p);

void tst ()
{
  X x [[uninit]];
  f (&x);
  g (&x); // { dg-error "is not marked" }
  // { dg-error "call to prove it initialized" "" { target *-*-* } .-1 }
  std::now_init_in_place (x);
  g (&x);
}
