// P3446R0/P4296R0 Invalidation profile, Phase 7b: Rule #0 is
// documented as NOT distinguishing two different instances of the
// SAME class template (P4296R0's own field-recursion or origin/cset
// machinery could, in principle; this increment's simpler
// class-template-identity check cannot, by design -- see this
// checker's own top comment in invalidation-profile-gimple.cc) -- a
// value bound to one container of a given template is still flagged
// after mutating a DIFFERENT container of the exact same template,
// confirming the checker declines rather than unsoundly granting Rule
// #0 here.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

template<typename T> struct ToyListIterator { T *p; };

template<typename T> struct ToyList
{
  [[not_invalidating]] ToyListIterator<T> begin () { return ToyListIterator<T> {}; }
  ToyListIterator<T> erase (ToyListIterator<T> it) { return it; }
};

struct Element { };

void other_use (ToyListIterator<Element>) { }

void
two_lists_same_template_bad (ToyList<Element> &elem, ToyList<Element> &other)
{
  auto iter = elem.begin ();
  other.erase (other.begin ());
  other_use (iter); // { dg-error "potentially invalidated by an earlier mutation" }
}
