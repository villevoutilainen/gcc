// P3446R0 Invalidation profile: a genuine false negative found via
// https://godbolt.org/z/PPGWa13fM -- a constructor's own 'const T&'/
// 'T&&' parameter, bound directly into a reference DATA MEMBER via
// the member-initializer-list, used to go completely unchecked: a
// constructor has no return value for ip_check_return_escape's own
// machinery to examine, so nothing ever asked whether 'ohnoes(r)'
// itself could dangle.
//
// The member persists for the constructed object's own lifetime,
// which is guaranteed to extend past this one constructor call -- the
// exact same question ip_check_return_escape asks of a return value
// (does this escape the current function's own activation) applies
// here too, so it must be flagged the same way 'const int* g(const
// int& x) { return &x; }' already is, REGARDLESS of what any
// particular caller happens to pass -- this checker can never know,
// from the constructor's own body, whether a given caller's argument
// is a temporary or not.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct X
{
  const int &ohnoes;
  X (const int &r) : ohnoes (r) {} // { dg-error "may not outlive this object" }
};
