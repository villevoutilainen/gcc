// P3446R0/P4296R0 Invalidation profile: the paper's own full worked
// example (vi1 mutated safely, vi2 mutated while a raw pointer is
// bound to it) -- confirms the diagnostic correctly names the
// CONTAINER ACTUALLY MUTATED ('vi2'), not the unrelated 'vi1' also
// mutated earlier (ip_decls_provably_distinct_objects_p keeps that
// mutation from being conflated with vi2's own binding at all).
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
  f (vi1);   // OK, no pointer to a vi1 element; reallocation is OK

  vector<int> vi2 { 1, 2 };
  auto p = vi2.begin ();   // point to first element of vi2

  f (vi2);

  *p = 7; // { dg-error "use of a value bound to .vi2., potentially invalidated by an earlier mutation of .vi2." }
}
