// P4222 Initialization profile, GIMPLE checker: a [[uninit]] local
// assigned on only one arm of an if is rejected at the merge point --
// definitely assigned means assigned on every incoming path.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int use (int);

void f (bool c)
{
  [[uninit]] int x;
  if (c)
    x = 1;
  use (x); // { dg-error "read before it is definitely assigned" }
}
