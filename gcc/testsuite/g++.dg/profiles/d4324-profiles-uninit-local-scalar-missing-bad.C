// P4222 Initialization profile, Increment 1 (local scalars only): a
// local scalar variable with no initializer and no [[uninit]] is
// rejected once the std::init profile is enforced.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void f ()
{
  int x; // { dg-error "not initialized and not marked" }
  (void) x;
}
