// P3589 Phase 5: the auto-exemption d4324-profiles-system-header-
// exempt-ok.C relies on is genuinely keyed on system-header status,
// not a blanket "profile checking is now off" switch -- ordinary
// code in the main translation unit (never a system header) still
// trips the usual diagnostic, with no explicit profiles::exempt
// needed to make that happen.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int use_it ()
{
  int x; // { dg-error "not initialized and not marked" }
  return x;
}
