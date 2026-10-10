// Negative control for d4324-profiles-invalidation-empty-return-
// type-ok.C: a return type with a real pointer field genuinely could
// carry an argument's address out, and the checker cannot see f1's
// body to rule that out -- must still be diagnosed, confirming the
// new type-capacity gate in ip_call_escapes_locally_p didn't
// over-widen what counts as safe.
//
// 'local' (a genuinely-named local, not a literal/temporary) is
// deliberate: binding f1's own 'const int&' parameter to a named,
// already-addressable local needs no anonymous materialization temp
// at all (confirmed via gdb: '&local' binds directly) -- so this
// stays outside the "see through a reference-binding temporary"
// trust ip_escapes_locally_p's own ADDR_EXPR branch now applies
// (see that function's own comment): passing an actual TEMPORARY to
// f1 -- e.g. 'f1(7)' -- is now trusted, symmetrically with
// push_back-style calls, on the same premise (rare misuse; if f1
// itself is compiled under enforce(), ITS OWN definition would catch
// a genuine dangling-retention bug) -- but 'local' has its own,
// independent lifetime extending beyond this one call, so there is
// nothing here for that premise to apply to; this must stay exactly
// as conservative as ever.
// { dg-do compile { target c++14 } }

[[profiles::enforce(std::invalidation)]];

struct X
{
  int *a;
};

X f1 (int const &m);

auto g1 ()
{
  int local = 7;
  return f1 (local); // { dg-error "may hold a pointer to a local" }
}
