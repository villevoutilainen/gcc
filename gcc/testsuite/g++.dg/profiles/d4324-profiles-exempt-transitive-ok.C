// P3589 Phase 5: [[profiles::exempt]] is transitive
// (profiles_header_exempt_p, profiles.cc) -- exempting only the
// directly-#included "outer" header also covers the "inner" header
// it in turn #includes, even though the inner header's own spelling
// ("d4324-profiles-exempt-transitive-inner.h") is never named by any
// exemption here.  Without transitivity this would still trip the
// usual "not initialized and not marked [[uninit]]" diagnostic inside
// the inner header.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];
[[profiles::exempt(std::init, quote_header: "d4324-profiles-exempt-transitive-outer.h")]];

#include "d4324-profiles-exempt-transitive-outer.h"

int use_it () { return legacy_uninit_pattern (); }
