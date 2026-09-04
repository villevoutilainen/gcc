// P4222 Initialization profile: a declaration marked [[uninit]] --
// "no promise is made about this object's contents" -- that ALSO has
// a real initializer is self-contradictory and rejected, both as a
// local object definition and as a class member with a default
// member initializer (NSDMI). The whole point of [[uninit]] is to opt
// out of requiring an initializer, not to be paired with one.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void f ()
{
  int x [[uninit]] = 42; // { dg-error "but also has an initializer" }
  (void) x;
}

struct S
{
  int m [[uninit]] = 42; // { dg-error "but also has an initializer" }
};
