// P3446R0/P4296R0 Invalidation profile: with no
// profiles::enforce(std::invalidation) in the translation unit, the
// Rule #0/#1 checker (and the Negative Baseline it narrows) never
// runs at all -- ordinary C++ rules apply, including to the
// same-container case that would otherwise be flagged.
// { dg-do compile { target c++11 } }

template<typename T> struct ToyListIterator { T *p; };

template<typename T> struct ToyList
{
  ToyListIterator<T> begin () { return ToyListIterator<T> {}; }
  ToyListIterator<T> erase (ToyListIterator<T> it) { return it; }
};

struct Element { };

void other_use (ToyListIterator<Element>) { }

void
f (ToyList<Element> &elem)
{
  auto iter = elem.begin ();
  elem.erase (iter);
  other_use (iter);
}
