// P3446R0/P4296R0 Invalidation profile: the identical shape
// d4324-profiles-invalidation-iterator-dereference-mutation-bad.C
// rejects, minus the intervening mutating call -- confirms that
// -bad.C's rejection is genuinely due to tracking the mutation, not
// the iterator's own dereference being flagged unconditionally.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

#include <initializer_list>

template <class T> struct iterator
{
  T *p;
  T &operator* ();
};

template <class T> struct vector
{
  void push_back (const T &);
  vector (std::initializer_list<T>);
  iterator<T> begin ();
};

void g ()
{
  vector<int> vi { 1, 2 };
  auto p = vi.begin ();
  *p = 7;
}
