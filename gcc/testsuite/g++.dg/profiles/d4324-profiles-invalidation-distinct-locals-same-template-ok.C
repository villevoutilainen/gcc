// P3446R0/P4296R0 Invalidation profile: two SEPARATELY DECLARED local
// variables of the exact same container template are provably
// distinct objects (ip_decls_provably_distinct_objects_p,
// invalidation-profile-gimple.cc) regardless of Rule #0's own
// template-identity comparison, which alone cannot tell them apart --
// mutating one must not taint a value bound to the OTHER.  vi2 (the
// container p is bound to) is never mutated anywhere in this
// function; only the completely unrelated vi1 is.  This is
// deliberately narrower than Rule #0 itself: it applies only to two
// ordinary local variables (VAR_DECL), never a reference/pointer
// PARAMETER, whose own underlying object identity is supplied by the
// caller and so genuinely could alias another parameter of the same
// type in the same call -- see d4324-profiles-invalidation-rule0-
// same-template-bad.C's own identical-looking shape, with parameters
// instead of locals, which is still correctly rejected.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

#include <initializer_list>

template <class T> struct vector
{
  void push_back (const T &);
  vector (std::initializer_list<T>);
  int *begin ();
};

void f (vector<int> &vi) { vi.push_back (9); }   // may relocate vi's elements

void g ()
{
  vector<int> vi1 { 1, 2 };
  vector<int> vi2 { 1, 2 };
  auto p = vi2.begin ();   // point to first element of vi2

  f (vi1);   // mutates vi1 -- a completely different, unrelated local

  *p = 7;   // OK: nothing has mutated vi2, which p is bound to
}
