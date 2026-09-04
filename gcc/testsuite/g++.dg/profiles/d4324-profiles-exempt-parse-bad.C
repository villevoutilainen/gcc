// P3589 Phase 5: profiles::exempt's own argument grammar is checked
// -- an unknown profile name and an unrecognized header-kind keyword
// are both rejected.
// { dg-do compile { target c++11 } }

[[profiles::exempt(bogus_profile, angle_header: "vector")]]; // { dg-error "unknown profile" }
[[profiles::exempt(std::init, wrong_kind: "vector")]]; // { dg-error "expected" }
