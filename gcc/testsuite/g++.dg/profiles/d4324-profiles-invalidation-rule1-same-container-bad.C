// P3446R0/P4296R0 Invalidation profile, Phase 7b: a value proven
// bound (Rule #1) to a container is still flagged when read again
// after a real, separate mutation of that SAME container -- neither
// Rule #0 (types are identical, not unrelated) nor anything else in
// this increment can clear it, matching P4296R0's own default-deny
// stance.  Distinguishes this from the "read as the mutating call's
// own argument" exemption d4324-profiles-invalidation-rule0-rule1-ok.C
// relies on: here the second use is a genuinely separate statement.
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
same_container_bad (ToyList<Element> &elem)
{
  auto iter = elem.begin ();
  elem.erase (iter);
  other_use (iter); // { dg-error "potentially invalidated by an earlier mutation" }
}
