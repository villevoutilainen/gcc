// P3589: -fprofiles-warning=<profile> enables a profile for the whole
// translation unit, same as -fprofiles-enforce=, but every violation
// is reported as a WARNING rather than a hard error -- the compile
// itself must still succeed.
// { dg-do compile { target c++11 } }
// { dg-options "-fprofiles-warning=std::init" }

void f ()
{
  int x; // { dg-warning "not initialized and not marked" }
  (void) x;
}
