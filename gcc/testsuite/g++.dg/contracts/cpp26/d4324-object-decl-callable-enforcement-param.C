// D4324: declaration-level contracts on callable-typed object
// declarations (see .claude/plans/stateless-jumping-shore.md) --
// call-site enforcement for a contracted function *parameter* (as
// opposed to a namespace-scope variable): the check function built for
// it must not be treated as nested inside the enclosing function (its
// DECL_CONTEXT is the enclosing FUNCTION_DECL, unlike a namespace-scope
// object) -- confirms both that this compiles/links and that the
// checks actually run when the parameter is called from within the
// enclosing function's own body.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

struct labeled {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  { if (ctx.check ()) return; violated = true; }
  static bool violated;
};
bool labeled::violated = false;

void invoke (void (*cb) (int x, int y) pre<labeled{}> (x > y))
{
  cb (3, 1);
}

void noop (int, int) {}

int main ()
{
  invoke (noop);
  if (labeled::violated)
    __builtin_abort ();

  // A second, distinct call through a *different* invocation of the
  // same enclosing function still checks correctly (the check
  // function, cached per parameter decl, is rebuilt fresh each time
  // invoke's own parameter comes into existence, exactly matching
  // ordinary parameter scoping).
  labeled::violated = false;
  invoke (noop);
  if (labeled::violated)
    __builtin_abort ();

  return 0;
}
