// Auxiliary header shared by d4324-profiles-system-header-exempt-ok.C
// (included via '-isystem', so this file IS a system header) and
// d4324-profiles-system-header-not-exempt-bad.C (included via an
// ordinary quote-include of the test's own directory, so this same
// content is NOT a system header there) -- a legacy-style function
// that would normally trip the std::init profile's "not initialized
// and not marked [[uninit]]" diagnostic.
inline int legacy_uninit_pattern ()
{
  int x;
  x = 5;
  return x;
}
