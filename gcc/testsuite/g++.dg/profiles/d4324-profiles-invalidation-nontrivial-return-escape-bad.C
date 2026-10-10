// P3446R0 Invalidation profile: a non-trivially-copyable return type
// (a user-declared destructor is enough, without depending on
// libstdc++ headers) that structurally holds a pointer -- confirmed a
// real false-negative bug (https://godbolt.org/z/3z1TPTvGf's
// std::vector<int> case): copy elision chains all the way through the
// caller's own return slot for such a type, so the retval
// ip_check_return_escape sees is an SSA_NAME wrapping the function's
// own RESULT_DECL ('struct X & _3(D); ... *_3(D) = f1 (&tmp); return
// _3(D);', confirmed via gdb), not a bare RESULT_DECL. Before this
// fix, ip_escapes_locally_p's SSA_NAME branch treated this exactly
// like an ordinary parameter's own default-def ("never &local"),
// skipping analysis of f1's arguments entirely.
//
// The argument shape here deliberately avoids the reference-binding
// trust (see ip_call_argument_escapes_locally_p's own comment, which
// now covers a genuinely-named decl's own address taken directly to
// bind a reference parameter, not just a materialized temporary --
// see d4324-profiles-invalidation-nonempty-return-type-bad.C's own
// updated comment for the full reasoning). 'ref' is a reference into
// a std::vector element, bound to a LOCAL BY-VALUE PARAMETER's own
// storage ('v'), read directly with no top-level ADDR_EXPR of a named
// decl at the f1(ref) call site at all -- so the widened trust never
// even applies, and f1's retention of it must still be assumed, same
// reasoning as d4324-profiles-invalidation-escape-opaque-call-bad.C's
// own 'g(ref)'.
// { dg-do compile { target c++14 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

struct X
{
  ~X ();
  int *a;
};

X f1 (int const &m);

auto g1 (std::vector<int> v)
{
  auto &ref = v[0];
  return f1 (ref); // { dg-error "may hold a pointer to a local" }
}
