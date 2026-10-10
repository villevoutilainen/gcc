// P3446R0 Invalidation profile: the reference-binding trust already
// extended to an anonymous, compiler-synthesized materialization temp
// (see d4324-profiles-invalidation-escape-seethrough-temporary-trust-
// ok.C) is widened here to also cover a genuinely-named decl's own
// address, taken directly to bind a reference parameter.
//
// 'v.push_back(p)': 'p' is an incoming 'int*' parameter, exactly
// matching push_back's own 'const int*&' element type -- binding it
// needs no intermediate temp, '&p' binds the reference directly.
// Confirmed via gdb this produces GIMPLE bitwise identical to an
// ordinary, explicit '&x' passed to a plain, non-reference pointer
// parameter (see the sibling d4324-profiles-invalidation-escape-
// explicit-address-still-bad.C) -- the only way to tell them apart is
// the DECLARED PARAMETER TYPE at that position (a REFERENCE_TYPE here,
// confirming this is a pure reference-binding artifact, not the
// user's own by-value address computation).
//
// 'f1(local)': same reasoning for a named local rather than a
// parameter -- 'local's own address binds f1's 'const int&' directly.
// A reference parameter cannot itself tell, at runtime, whether it is
// bound to a temporary or a persistent, named object, so trusting one
// and not the other (for "does the callee retain the reference
// itself", which this checker can never prove either way, since it
// never inspects a callee's body) was never principled -- see
// ip_call_argument_escapes_locally_p's own comment in
// invalidation-profile-gimple.cc for the full reasoning.
// { dg-do compile { target c++14 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

std::vector<int*> f (int *p)
{
  std::vector<int*> v;
  v.push_back (p);
  return v;
}

struct X { int *a; };
X f1 (int const &m);

auto g1 ()
{
  int local = 7;
  return f1 (local);
}
