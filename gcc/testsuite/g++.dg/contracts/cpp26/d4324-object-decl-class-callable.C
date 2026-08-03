// D4324: declaration-level contracts on callable-typed object
// declarations, extended to class-type callables (see
// .claude/plans/stateless-jumping-shore.md) -- a pre<>/post<> clause
// attached to a class-type object declaration (a std::function, an
// ordinary functor, a non-generic lambda closure), requiring exactly
// one resolvable, non-template, non-overloaded operator() found via
// ordinary member lookup at the declaration: this is the grammar and
// semantic-attach slice, matching the already-shipped function-pointer
// case's own scope; call-site enforcement is separate, later work.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fsyntax-only" }

#include <contracts>
#include <functional>

struct ctrl { void operator() (const std::contracts::assertion_context&) const {} };
inline constexpr ctrl ctrl_v{};

// Ordinary functor.
struct adder {
  int operator() (int a, int b) const { return a + b; }
};

// Top-level variable of class-callable type, named-control and empty-control forms.
// The shortcut form (no binder list) reuses operator()'s own real
// parameter names ("a", "b") for ordinary lookup.
adder ad1 pre<ctrl_v> (a > 0 && b > 0);
adder ad2 pre<> (a > 0 && b > 0);

// std::function -- its own operator()'s real parameter names aren't a
// stable, guessable spelling, so use a binder list instead.
std::function<int (int, int)> fp1 pre<> (a, b: a > 0 && b > 0);
std::function<void (int)> fp2 post<> (x: x >= 0);

// A non-generic lambda closure type; a lambda's own parameter is
// named "x" here, so the shortcut form applies directly.
auto lam = [] (int x) { return x > 0; };
decltype(lam) lam_obj pre<> (x > 0);

struct S
{
  // Static data member of class-callable type.
  static adder mad pre<> (a > 0 && b > 0);
};

// A function parameter of class-callable type.
void f (adder cb pre<> (a > 0 && b > 0));
void g (std::function<void (int)> cb pre<> (v: v >= 0));

// A class template instantiated down to a single, concrete, ordinary
// (non-template) operator() is accepted, exactly like std::function
// above -- only a genuinely still-templated operator() (a member
// function template, or a generic lambda) is out of scope.
template<class T>
struct generic { T operator() (T x) const { return x; } };
generic<int> gen pre<> (x > 0);

int main () { return 0; }
