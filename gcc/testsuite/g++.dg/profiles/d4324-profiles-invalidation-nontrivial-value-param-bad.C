// P3446R0 Invalidation profile: a minimal, hand-written analogue of the
// user-reported std::string_view case (std::string/std::string_view
// themselves are avoided here -- pulling in <string> under this test
// harness's own plain -I, not -isystem, convention surfaces a flood of
// unrelated, pre-existing diagnostics deep in libstdc++'s own internals,
// nothing to do with this fix). Owner has a user-declared destructor, so
// it is non-trivially-copyable and passed via the Itanium ABI's
// invisible-reference convention even though declared by value -- x's
// own PARM_DECL has REFERENCE_TYPE *and* DECL_BY_REFERENCE, confirmed via
// gdb to be exactly the same GIMPLE shape std::string's own by-value
// parameter reaches. x is exclusively owned by this one call; View's
// own pointer into it dangles the moment f returns.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct Owner
{
  ~Owner () {}
  int data[4];
};

struct View
{
  const int *p;
  View (const Owner &o) : p (o.data) {}
};

View f (Owner x)
{
  return View (x); // { dg-error "may hold a pointer to a local" }
}
