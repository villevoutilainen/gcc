// P3446R0 Invalidation profile: a traced side effect of the anonymous-
// temp "see through" fix, not a new, separate mechanism. push_back's
// own argument here is get_pointer(p)'s return value, materialized
// into the same kind of anonymous temp the sibling escape-seethrough-
// temp-ok.C test covers -- recursing into it reaches get_pointer's
// own call, and the existing, unrelated, already-correct rule for an
// ordinary received pointer argument (the same one that already
// proves 'int* received(int* p) { return p; }' safe) shows
// get_pointer's own argument doesn't dangle, so its return value
// doesn't either. No std::no_dangling()/std::no_escape() needed at
// all -- unlike a case where the argument traces to something
// genuinely local (see the opaque-call-bad.C test elsewhere in this
// suite, where the argument is a reference into a local by-value
// parameter instead).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

int* get_pointer (int* p) { return p; }

std::vector<int*> f (int* p)
{
  std::vector<int*> v;
  v.push_back (get_pointer (p));
  return v;
}
