// P3446R0/P4296R0 Invalidation profile: a class-typed iterator's own
// DEREFERENCE ('*p'/'p.operator* ()') is now tracked as a use of p,
// not just p being passed by value to another function (the only
// shape d4324-profiles-invalidation-rule1-same-container-bad.C
// exercised before this).  A member-function call's receiver is, at
// the GIMPLE level, the call's first argument taken by ADDRESS (&p),
// not p directly -- ip_use_decl (invalidation-profile-gimple.cc) now
// resolves that shape the same way ip_receiver_decl already did for a
// MUTATING call's own receiver.  Uses an iterator with real state (a
// pointer member), not an empty class: an empty iterator's own
// binding statement can be elided entirely at gimplification (before
// any GIMPLE pass ever runs, confirmed directly via -fdump-tree-
// gimple), which is a fact about empty classes carrying no data to
// materialize, not a limitation of this tracking.
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

void f (vector<int> &vi) { vi.push_back (9); }   // may relocate vi's elements

void g ()
{
  vector<int> vi { 1, 2 };
  auto p = vi.begin ();   // point to first element of vi
  f (vi);                 // mutates vi
  *p = 7; // { dg-error "potentially invalidated by an earlier mutation" }
}
