// P4222 Initialization profile, GIMPLE checker: a [[uninit]] local
// read before any assignment is rejected.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int use (int);

void f ()
{
  [[uninit]] int x;
  use (x); // { dg-error "read before it is definitely assigned" }
  x = 5;
}
