// P3589: an unrecognized profile name in profiles::suppress is a hard
// error, matching profiles::enforce's and profiles::exempt's own
// identical "unknown profile" diagnostic for the same situation --
// suppress's own attribute-argument grammar accepts an arbitrary
// dotted identifier without validating it, so cp_finish_decl's call to
// profiles_register_suppression is the first point that can.
// { dg-do compile { target c++11 } }

void f ()
{
  [[profiles::suppress(bogus_profile)]] int x; // { dg-error "unknown profile" }
  (void) x;
}
