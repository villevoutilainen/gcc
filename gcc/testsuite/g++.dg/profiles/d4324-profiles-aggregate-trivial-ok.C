// P4222 Initialization profile, Phase 4e (S5.4): a non-union class
// type with no user-declared constructor and a trivial default
// constructor (every member left genuinely indeterminate by default-
// init, none has its own real constructor) is treated exactly like a
// scalar -- a full initializer at the declaration is accepted.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct P {
  int x;
  int y;
};

void f ()
{
  P p1 = {1, 2};
  (void) p1;
}
