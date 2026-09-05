// P3446R0/P4296R0 Invalidation profile: the identical shape
// d4324-profiles-invalidation-raw-pointer-mutation-bad.C rejects,
// minus the intervening mutating call -- confirms that -bad.C's
// rejection is genuinely due to tracking the mutation, not the raw
// pointer's own dereference being flagged unconditionally (the old,
// now-removed Negative-Baseline blanket ban).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

#include <initializer_list>

template <class T> struct vector
{
  void push_back (const T &);
  vector (std::initializer_list<T>);
  T *begin ();
};

void g ()
{
  vector<int> vi { 1, 2 };
  auto p = vi.begin ();
  *p = 7;
}
