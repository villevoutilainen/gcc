// P3589 Phase 5: system headers are auto-exempt from every enforced
// profile, unconditionally, with no explicit profiles::exempt needed
// (profiles_header_exempt_p's own in_system_header_at check,
// profiles.cc) -- '-isystem $srcdir/g++.dg/profiles' makes the aux
// header below a genuine system header, so its own uninitialized-
// and-unmarked local is never even considered, no exemption declared
// at all.
// { dg-do compile { target c++11 } }
// { dg-additional-options "-isystem $srcdir/g++.dg/profiles" }

[[profiles::enforce(std::init)]];

#include <d4324-profiles-system-header-exempt-aux.h>

int use_it () { return legacy_uninit_pattern (); }
