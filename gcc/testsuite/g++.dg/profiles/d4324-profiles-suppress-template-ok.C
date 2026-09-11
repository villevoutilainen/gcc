// P3589: profiles::suppress attached inside a function TEMPLATE must
// not corrupt template instantiation. is_late_template_attribute
// (decl2.cc) treats any bare IDENTIFIER_NODE attribute argument as
// "an unresolved name -- must be dependent" (value_dependent_
// expression_p's own IDENTIFIER_NODE case) -- exactly wrong for
// profiles::suppress's own profile-name argument, which is
// deliberately never looked up at all (cp_parser_profile_designator's
// own comment). Without decl2.cc's own profiles::suppress exemption
// from that generic check, attaching the attribute to (or inside) a
// template entity marked it ATTR_IS_DEPENDENT, deferring it to
// instantiation time, where tsubst_attribute's generic tsubst_expr
// fallback tried to look the profile-name identifier up as a real
// expression and failed with a bogus "'std::init' was not declared in
// this scope" during instantiation, instead of ever reaching
// profiles.cc's own suppress handling.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

template <class T>
void f (T)
{
  [[profiles::suppress(std::init)]] int x;
  (void) x;
}

void g ()
{
  f (0);
}
