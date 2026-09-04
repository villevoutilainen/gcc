// P3446R0/P4296R0 Invalidation profile, Phase 7b: Rule #0 ("Unrelated
// Types don't Interact") composing with Rule #1 ("Patently
// Independent Containers don't Interact") to accept the exact demo
// from P4296R0 S7.6.2 (also the CppCon 2026 "Profiles" talk's own
// invalidation prototype demo, slide 41: "list element into a
// vector"): pushing an iterator bound to a list-shaped container into
// an unrelated vector-shaped container, then re-binding it via
// erase() on the SAME list-shaped container, must not be flagged.
//
// Toy container templates stand in for std::list/std::vector here
// (rather than the real containers) because making the real
// libstdc++ headers themselves compile clean under this profile needs
// a much broader "annotate/exempt the whole standard library" sweep,
// explicitly out of scope for this increment (see the profiles plan's
// own Phase 7b notes) -- these toys use the exact same structural
// idiom real containers do (an iterator class template sharing its
// element-type template argument with its container, e.g. libstdc++'s
// "typedef _List_iterator<_Tp> iterator;" inside list<_Tp>), so they
// exercise the checker's real Rule #0/#1 logic, not a special case
// carved out for the test.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

template<typename T> struct ToyListIterator { T *p; };

template<typename T> struct ToyList
{
  [[not_invalidating]] ToyListIterator<T> begin () { return ToyListIterator<T> {}; }
  [[not_invalidating]] ToyListIterator<T> end () { return ToyListIterator<T> {}; }
  ToyListIterator<T> erase (ToyListIterator<T> it) { return it; }
};

template<typename T> struct ToyVector
{
  void push_back (ToyListIterator<T> it) { }
};

struct Element { };

bool can_process (ToyListIterator<Element>) { return true; }

ToyVector<Element>
process_elements (ToyList<Element> &elem)
{
  ToyVector<Element> result;
  auto iter = elem.begin ();
  if (can_process (iter))
    {
      result.push_back (iter);   // Rule #0: ToyVector and ToyList are
				  // unrelated templates -- cannot alias.
      iter = elem.erase (iter);  // iter read here as erase()'s own
				  // argument, evaluated before erase()'s
				  // own mutation of elem takes effect.
    }
  return result;
}
