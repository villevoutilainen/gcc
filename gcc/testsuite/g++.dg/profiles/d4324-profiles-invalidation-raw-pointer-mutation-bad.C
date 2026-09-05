// P3446R0/P4296R0 Invalidation profile: a raw pointer bound to a
// container via a "begin()"-shaped accessor (ip_pointer_return_
// binds_p, invalidation-profile-gimple.cc) is tracked exactly like a
// class-typed iterator/handle already was -- dereferencing it after a
// mutating call on that same container is flagged, the same
// "use of a value bound to ..., potentially invalidated by an earlier
// mutation of ..." diagnostic Rule #0/#1 already give a class-typed
// value.  This replaces the old Negative-Baseline blanket dereference
// ban (see d4324-profiles-invalidation-dereference-ok.C) with real
// tracking instead of an unconditional rejection.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

#include <initializer_list>

template <class T> struct vector
{
  void push_back (const T &);
  vector (std::initializer_list<T>);
  T *begin ();
};

void f (vector<int> &vi) { vi.push_back (9); }   // may relocate vi's elements

void g ()
{
  vector<int> vi { 1, 2 };
  auto p = vi.begin ();   // point to first element of vi
  f (vi);                 // mutates vi
  *p = 7; // { dg-error "potentially invalidated by an earlier mutation" }
}
