// P3589: companion to d4324-profiles-enforce-declaration-only-bad.C,
// for profiles::exempt, and for the OTHER misattachment shape: a
// missing ';' letting the attribute-specifier-seq attach to a
// following declaration that itself defines a class type ('struct
// Foo {};', with no separate object being declared). This shape never
// reaches decl_attributes at all -- check_tag_decl (decl.cc) calls
// warn_misplaced_attr_for_class_type directly, which used to give
// only the same generic "attribute ignored" warning regardless of
// which attribute was misplaced. Confirms profiles_diagnose_
// misplaced_enforce_or_exempt (profiles.cc) is wired into that path
// too, not just decl_attributes's own.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];
[[profiles::exempt(std::init, angle_header: "vector")]]

struct Foo {}; // { dg-error "only appertains to its own" }
