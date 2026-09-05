// P3589: -fprofiles-enforced=<profile> enforces a profile for the
// whole translation unit non-intrusively, from the command line,
// without any '[[profiles::enforce]]' in the source at all -- letting
// existing, unmodified code be compiled under an enforced profile.
// { dg-do compile { target c++11 } }
// { dg-options "-fprofiles-enforced=std::init" }

void f ()
{
  int x; // { dg-error "not initialized and not marked" }
  (void) x;
}
