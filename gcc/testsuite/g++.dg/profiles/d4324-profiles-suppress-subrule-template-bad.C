// P3589: the combination that motivated d4324-profiles-suppress-
// subrule-bad.C and d4324-profiles-suppress-template-ok.C -- a
// profiles::suppress with an unrecognized 'rule:' argument attached to
// a function TEMPLATE, instantiated at least once. Must give the same
// "no sub-rule" diagnostic at both the template definition and (once
// more, independently, per instantiation -- correctly, since the
// instantiation is its own declaration with its own copy of the
// attribute) the instantiation, and never the bogus "'std::init' was
// not declared in this scope" that decl2.cc's is_late_template_
// attribute fix (see d4324-profiles-suppress-template-ok.C's own
// comment) was written to prevent.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

template <class T>
[[profiles::suppress(std::init, rule: "ref_to_uninit")]]
T *now_init (T *p) { return p; } // { dg-error "has no sub-rule" }
// One dg-error here matches BOTH the definition-time and the (once
// more, correctly independent) instantiation-time occurrence of the
// same diagnostic -- dg-error's own matching regsub is "-all" over
// runs of consecutive matching lines, which absorbs every such run in
// the whole compiler output, not just the first.

void f (int *p)
{
  now_init (p);
}
