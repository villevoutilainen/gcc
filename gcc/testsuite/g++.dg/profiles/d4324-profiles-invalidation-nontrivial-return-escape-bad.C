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
  return f1 (7); // { dg-error "may hold a pointer to a local" }
}
