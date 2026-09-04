// Auxiliary header for d4324-profiles-exempt-transitive-ok.C: only
// ever reached by #include from d4324-profiles-exempt-transitive-
// outer.h, never directly -- exercises exemption transitivity
// (profiles_header_exempt_p, profiles.cc), not a direct-include match.
inline int legacy_uninit_pattern ()
{
  int x;
  x = 5;
  return x;
}
