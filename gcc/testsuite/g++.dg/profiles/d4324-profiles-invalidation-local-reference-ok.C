// Negative control for d4324-profiles-invalidation-local-reference-bad.C:
// a reference bound to something genuinely unrelated to any mutated
// container must stay clean -- confirms the fix (ip_trackable_operand_p/
// ip_deref_base_decl now covering REFERENCE_TYPE) didn't over-widen via
// Rule #0's own type-relatedness check.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

#include <initializer_list>

template <class T> struct vector
{
  void push_back (const T &);
  vector (std::initializer_list<T>);
  T &front ();
};

void f (vector<int> &vi) { vi.push_back (9); }

int g ()
{
  vector<int> vi { 1, 2 };
  int other = 42;
  int const &x = other;   // unrelated to vi entirely
  f (vi);                 // mutates vi, not x's own referent
  return x;
}
