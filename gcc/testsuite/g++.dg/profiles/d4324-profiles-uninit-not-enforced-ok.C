// P4222 Initialization profile: with no profiles::enforce(std::init)
// in the translation unit, an uninitialized local scalar is
// unaffected -- ordinary C++ rules apply.
// { dg-do compile { target c++11 } }

void f ()
{
  int x;
  (void) x;
}
