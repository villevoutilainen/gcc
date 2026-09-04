// P3589 Phase 5: [[profiles::exempt(profile, quote_header: "NAME")]]
// suppresses the named profile's diagnostics for code whose location
// was reached via a '#include "NAME"' with that exact spelling --
// confirmed via direct testing (see init-profile-gimple.cc's own
// commit) that without this exempt declaration, the auxiliary
// header's own local variable trips the usual "not initialized and
// not marked [[uninit]]" diagnostic.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];
[[profiles::exempt(std::init, quote_header: "d4324-profiles-exempt-aux.h")]];

#include "d4324-profiles-exempt-aux.h"

int use_it () { return legacy_uninit_pattern (); }
