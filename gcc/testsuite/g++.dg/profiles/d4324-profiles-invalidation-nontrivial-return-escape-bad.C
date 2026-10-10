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
// 'local' (a genuinely-named local, not a literal/temporary) is
// deliberate -- see d4324-profiles-invalidation-nonempty-return-
// type-bad.C's own comment: binding f1's own 'const int&' parameter
// to a named local needs no anonymous materialization temp (it binds
// directly), so it stays outside the "see through a reference-
// binding temporary" trust ip_escapes_locally_p's own ADDR_EXPR
// branch now applies to an actual temporary passed to an opaque call
// (symmetric with push_back-style calls, same premise: rare misuse,
// caught at f1's own definition if it's also compiled under
// enforce()) -- local's own lifetime independently extends beyond
// this call, so this must stay exactly as conservative as ever.
// { dg-do compile { target c++14 } }

[[profiles::enforce(std::invalidation)]];

struct X
{
  ~X ();
  int *a;
};

X f1 (int const &m);

auto g1 ()
{
  int local = 7;
  return f1 (local); // { dg-error "may hold a pointer to a local" }
}
