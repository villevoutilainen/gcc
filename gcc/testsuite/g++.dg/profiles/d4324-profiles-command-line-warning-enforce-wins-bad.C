// P3589: -fprofiles-enforce=<profile> wins over -fprofiles-warning=
// for the same profile named by both -- an explicit enforce request
// is never weakened to a warning.
// { dg-do compile { target c++11 } }
// { dg-options "-fprofiles-warning=std::init -fprofiles-enforce=std::init" }

void f ()
{
  int x; // { dg-error "not initialized and not marked" }
  (void) x;
}
