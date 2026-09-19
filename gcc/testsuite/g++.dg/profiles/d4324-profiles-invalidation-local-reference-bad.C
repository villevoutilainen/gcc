// P3446R0/P4296R0 Invalidation profile: a reference bound to a container
// element is tracked exactly like a raw pointer or class-typed iterator
// already was -- ip_trackable_operand_p (invalidation-profile-gimple.cc)
// never included REFERENCE_TYPE at all, so this exact shape (a local
// reference, not even a parameter) was silently invisible to this
// checker, despite a local raw pointer or iterator bound the same way
// already being correctly flagged. References are already lowered to
// pointers at the GIMPLE level, so nothing structurally distinguished
// them from the already-working raw-pointer case.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

#include <initializer_list>

template <class T> struct vector
{
  void push_back (const T &);
  vector (std::initializer_list<T>);
  T &front ();
};

void f (vector<int> &vi) { vi.push_back (9); }   // may relocate vi's elements

void g ()
{
  vector<int> vi { 1, 2 };
  int const &x = vi.front ();   // refers to the first element of vi
  f (vi);                       // mutates vi
  int y = x; // { dg-error "potentially invalidated by an earlier mutation" }
  (void) y;
}
