// Same as d4324-profiles-eager-check-both-diagnosed-bad.C, but with
// the unrelated front-end error appearing FIRST in the translation
// unit rather than last -- confirms the eager per-function checking
// mechanism doesn't depend on source ordering: it must reach g()'s
// own std::init violation regardless of whether h()'s front-end
// error was already seen by the time g() is parsed.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int h ()
{
  return undeclared_identifier; // { dg-error "was not declared" }
}

int g ()
{
  int x [[uninit]];
  int *p [[ref_to_uninit]] = &x;
  return *p; // { dg-error "read before it is definitely assigned" }
}
