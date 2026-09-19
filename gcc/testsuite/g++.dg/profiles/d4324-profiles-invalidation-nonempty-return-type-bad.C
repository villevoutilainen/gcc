// Negative control for d4324-profiles-invalidation-empty-return-
// type-ok.C: a return type with a real pointer field genuinely could
// carry an argument's address out, and the checker cannot see f1's
// body to rule that out -- must still be diagnosed, confirming the
// new type-capacity gate in ip_call_escapes_locally_p didn't
// over-widen what counts as safe.
// { dg-do compile { target c++14 } }

[[profiles::enforce(std::invalidation)]];

struct X
{
  int *a;
};

X f1 (int const &m);

auto g1 ()
{
  return f1 (7); // { dg-error "may hold a pointer to a local" }
}
