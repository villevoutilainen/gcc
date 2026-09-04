// P3589 Phase 5: an exemption for an unrelated header name does not
// leak and suppress diagnostics for code that was never reached via
// that #include at all (here, code directly in the main file) --
// confirms d4324-profiles-exempt-quote-ok.C's clean compile is
// genuinely due to a matching exemption, not exemptions suppressing
// everything once any exemption exists.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];
[[profiles::exempt(std::init, quote_header: "some-other-header.h")]];

void f ()
{
  int x; // { dg-error "not initialized and not marked" }
  (void) x;
}
