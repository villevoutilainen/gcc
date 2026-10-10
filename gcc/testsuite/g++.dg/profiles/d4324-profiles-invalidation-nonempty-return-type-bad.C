// Negative control for d4324-profiles-invalidation-empty-return-
// type-ok.C: a return type with a real pointer field genuinely could
// carry an argument's address out, and the checker cannot see f1's
// body to rule that out -- must still be diagnosed, confirming the
// new type-capacity gate in ip_call_escapes_locally_p didn't
// over-widen what counts as safe.
//
// The reference-binding trust (see ip_call_argument_escapes_locally_p's
// own comment) now covers BOTH a materialized temporary ('f1(7)') and
// a genuinely-named decl's own address taken directly to bind f1's
// 'const int&' parameter ('int local = 7; f1(local);') -- a reference
// carries no runtime distinction between the two, so trusting one and
// not the other for "does f1 retain the reference itself" was never
// principled; see d4324-profiles-invalidation-escape-seethrough-named-
// arg-ok.C for that now-safe case.
//
// To keep exercising the type-capacity gate itself, 'ref' here is a
// reference into a std::vector element, bound to a LOCAL BY-VALUE
// PARAMETER's own storage ('v') -- read directly (no top-level
// ADDR_EXPR of a named decl at the f1(ref) call site at all, so the
// widened trust doesn't even apply), and the checker cannot rule out
// that f1 retains it, same reasoning as d4324-profiles-invalidation-
// escape-opaque-call-bad.C's own 'g(ref)'.
// { dg-do compile { target c++14 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

struct X
{
  int *a;
};

X f1 (int const &m);

auto g1 (std::vector<int> v)
{
  auto &ref = v[0];
  return f1 (ref); // { dg-error "may hold a pointer to a local" }
}
