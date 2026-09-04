// P3446R0 Invalidation profile: [[not_invalidating]] on a plain
// function's own parameter (not just on a non-const member function
// itself, d4324-profiles-invalidation-rule0-rule1-ok.C's own use)
// opts a specific non-const-reference argument out of the default
// P4296R0/CppCon-slide-53 assumption that a function invalidates it.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

template<typename T> struct ToyListIterator { T *p; };

template<typename T> struct ToyList
{
  [[not_invalidating]] ToyListIterator<T> begin () { return ToyListIterator<T> {}; }
};

struct Element { };

void inspect ([[not_invalidating]] ToyList<Element> &elem);
void other_use (ToyListIterator<Element>) { }

void f (ToyList<Element> &elem)
{
  auto iter = elem.begin ();
  inspect (elem);
  other_use (iter);
}
