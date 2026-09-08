// P3589: -Wno-profiles-init independently silences the warning
// -fprofiles-warning=std::init would otherwise print -- confirms
// these are real, individually addressable GCC warnings, not just an
// unconditional message this checker always prints once the profile
// is in warning mode.
// { dg-do compile { target c++11 } }
// { dg-options "-fprofiles-warning=std::init -Wno-profiles-init" }

void f ()
{
  int x;
  (void) x;
}
