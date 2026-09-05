// P3446R0/P4296R0 Invalidation profile: a REAL std::vector<int>::
// iterator (libstdc++'s own __normal_iterator<_Iterator, _Container>,
// not a toy template) is now tracked for genuine use-after-
// invalidation, not just the synthetic iterator shapes this project's
// own earlier tests used. Confirms ip_call_result_may_reference_
// receiver_p's own deliberately broad assumption (invalidation-
// profile-gimple.cc): __normal_iterator is parameterized on the
// CONTAINER TYPE ITSELF as its second template argument, which the
// checker's own earlier, narrower template-argument-SHARING heuristic
// (matching only a shared ELEMENT type, e.g. iterator<T>/vector<T>)
// could never recognize as bound to its receiver at all -- silently
// missing this exact case entirely before this fix.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

void f (std::vector<int> &vi) { vi.push_back (9); }

void g ()
{
  std::vector<int> vi { 1, 2 };
  auto p = vi.begin ();
  f (vi);
  *p = 7; // { dg-error "potentially invalidated by an earlier mutation" }
}
