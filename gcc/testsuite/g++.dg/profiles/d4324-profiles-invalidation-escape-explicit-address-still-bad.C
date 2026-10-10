// P3446R0 Invalidation profile: the two cases the widened reference-
// binding trust (see the sibling d4324-profiles-invalidation-escape-
// seethrough-named-arg-ok.C) deliberately does NOT sweep in -- both
// must stay exactly as conservative as ever.
//
// 'v.push_back(&local)': unlike 'push_back(p)' (p already
// addressable, binds directly), '&local' is a genuine, explicit
// computation nested one level inside an anonymous materialization
// temp ('D.xxx = &local; push_back(v, &D.xxx);', confirmed via gdb)
// -- NOT a call's own direct argument. The real risk here is that
// '&local' itself (the pointer value being stored) dangles, not
// whether 'local's own trivial int value does -- so this must keep
// going through the ordinary, conservative ADDR_EXPR handling, never
// recursed past.
//
// 'sink(&x)': 'sink' takes a plain, non-reference 'int*' -- the
// DECLARED PARAMETER TYPE at this position is what distinguishes this
// from 'push_back(p)' in the sibling -ok.C test, even though the two
// produce bitwise-identical GIMPLE (a bare ADDR_EXPR of a named decl,
// directly as the call's own argument) -- confirmed via gdb. '&x' is
// the user's own explicit, by-value address computation here, exactly
// as suspect as 'WidgetFactory(log)' in the lifetime-rules doc; the
// reference-binding trust only ever applies when the parameter being
// bound is itself a reference.
// { dg-do compile { target c++14 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

std::vector<int*> f ()
{
  std::vector<int*> v;
  int local = 0;
  v.push_back (&local);
  return v; // { dg-error "may hold a pointer to a local" }
}

struct X { int *a; };
X sink (int *p);

auto g1 ()
{
  int x = 0;
  return sink (&x); // { dg-error "may hold a pointer to a local" }
}
