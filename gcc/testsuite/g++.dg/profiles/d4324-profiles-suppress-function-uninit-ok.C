// P3589: a function-level '[[profiles::suppress(std::init)]]' must
// cover a plain local variable declared without an initializer and
// without '[[uninit]]' -- the diagnostic for that (decl.cc's
// cp_finish_decl, "not initialized and not marked [[uninit]]") fires
// immediately while the enclosing function's own body is still being
// parsed, well before finish_function's own end-of-body hook used to
// be the only place the function-level suppression range got
// registered at all. Without profiles_open_function_suppressions
// opening an (unbounded, until finish_function closes it) range at
// the very start of the function's body instead, this local variable
// was diagnosed anyway despite the attribute -- confirmed against
// d4324-profiles-suppress-function-scope-bad.C, which checks the
// exact opposite: that the suppression does NOT leak into a sibling
// function.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

[[profiles::suppress(std::init)]]
void f ()
{
  int x;
  (void) x;
}

template <class T>
[[profiles::suppress(std::init)]]
void g (T)
{
  int x;
  (void) x;
}

void h ()
{
  g (0);
}
