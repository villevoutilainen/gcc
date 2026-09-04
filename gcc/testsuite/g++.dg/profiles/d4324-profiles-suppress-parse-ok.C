// P3589: profiles::suppress's real semantic effect -- a declaration
// that would otherwise be diagnosed under the enforced profile (the
// exact same "int x;" shape d4324-profiles-uninit-local-scalar-missing-
// bad.C rejects, confirming this isn't accidentally undiagnosable for
// some other reason) compiles cleanly once profiles::suppress(std::init)
// is attached directly to it.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void f ()
{
  [[profiles::suppress(std::init)]] int x;
  (void) x;
}
