// P3446R0 Invalidation profile: a plain (non-member) function is
// assumed to invalidate a non-const container-typed argument, the
// same default a non-const member call already gets (CppCon 2026
// "Profiles" talk, slide 53) -- not just member calls.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

template<typename T> struct ToyListIterator { T *p; };

template<typename T> struct ToyList
{
  [[not_invalidating]] ToyListIterator<T> begin () { return ToyListIterator<T> {}; }
};

struct Element { };

void mutate (ToyList<Element> &elem);
void other_use (ToyListIterator<Element>) { }

void f (ToyList<Element> &elem)
{
  auto iter = elem.begin ();
  mutate (elem);
  other_use (iter); // { dg-error "potentially invalidated by an earlier mutation" }
}
