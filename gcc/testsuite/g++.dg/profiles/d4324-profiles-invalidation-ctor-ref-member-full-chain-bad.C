// P3446R0 Invalidation profile: the exact reproduction from
// https://godbolt.org/z/PPGWa13fM, showing the full chain this new
// check closes -- before this fix, this compiled clean end-to-end,
// structurally identical to the already-caught WidgetFactory/Logger
// example (escape-container-bad.C), except nothing in the chain ever
// examined 'ohnoes(r)' itself.
//
// 'h()' binds X's own ctor parameter directly to a named local
// ('X x(local)') -- trusted, no diagnostic at this call site (see
// d4324-profiles-invalidation-escape-seethrough-named-arg-ok.C).
// 'g()' reads the reference member directly and returns it -- also
// trusted (see d4324-profiles-invalidation-escape-ref-member-const-
// ok.C). Both of those trusts are sound only because the one place
// that CAN see the actual misuse -- X's own constructor, binding its
// 'const int&' parameter into 'ohnoes' -- is independently checked;
// exactly one error fires, there.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct X
{
  const int &ohnoes;
  X (const int &r) : ohnoes (r) {} // { dg-error "may not outlive this object" }

  const int &g ()
  {
    return ohnoes;
  }
};

const int &h ()
{
  int local = 7;
  X x (local);
  return x.g ();
}
