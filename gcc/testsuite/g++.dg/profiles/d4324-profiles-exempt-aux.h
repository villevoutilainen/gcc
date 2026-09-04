// Auxiliary header for d4324-profiles-exempt-quote-ok.C: a
// legacy-style function that would normally trip the std::init
// profile's "not initialized and not marked [[uninit]]" diagnostic.
inline int legacy_uninit_pattern ()
{
  int x;
  x = 5;
  return x;
}
