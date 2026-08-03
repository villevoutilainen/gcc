// D4324: declaration-level contracts on callable-typed object
// declarations, extended to class-type callables (see
// .claude/plans/stateless-jumping-shore.md) -- reject a class type
// whose operator() isn't uniquely resolvable at the declaration: an
// overloaded operator(), a template/generic operator() (including a
// generic lambda's own call operator), or no operator() at all.  Per
// direct user feedback, this feature deliberately does not defer
// resolution to each call site: an ambiguous or unresolvable
// operator() at the declaration itself is a hard error, not a
// silently-skipped clause.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fsyntax-only" }

#include <contracts>

struct overloaded {
  int operator() (int a) const { return a; }
  int operator() (int a, int b) const { return a + b; }
};
overloaded ov pre<> (a > 0); // { dg-error "requires a single, non-overloaded, non-template" }

// A genuine member function template operator() (as opposed to a
// class template instantiated down to a single, ordinary operator(),
// which is the whole point of this feature -- see std::function in
// the companion good test) -- still a template after instantiation of
// the enclosing (non-template) class, so still out of scope.
struct member_template {
  template<class T> T operator() (T x) const { return x; }
};
member_template mt pre<> (x > 0); // { dg-error "requires a single, non-overloaded, non-template" }

auto gl = [] (auto x) { return x; };
decltype(gl) genl pre<> (x > 0); // { dg-error "requires a single, non-overloaded, non-template" }

struct nocall {};
nocall nc pre<> (true); // { dg-error "requires a single, non-overloaded, non-template" }

int main () { return 0; }
