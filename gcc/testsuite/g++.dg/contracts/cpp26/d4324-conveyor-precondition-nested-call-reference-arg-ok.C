// D4324: build_predicate_arg_struct_var's own dedicated regression test.
// A REFERENCE parameter used as the argument to a NESTED conveyor call,
// reached from a precondition's own condition text (evaluated lazily via
// this file's own thunk/args-struct mechanism), previously computed the
// wrong address for that reference's own args-struct field -- one level
// of indirection too many (see build_predicate_arg_struct_var's own
// comment for the full account). The bug was invisible whenever the
// nested call's own precondition never actually read its argument (e.g.
// is_object_address, resolved to a compile-time constant) -- USE_VAL's
// own body genuinely reads X, so this is the isolable, minimal repro:
// it either returns the right answer every time, or (before the fix)
// nondeterministically trapped or returned garbage depending on
// whatever was left on the stack.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct conveyor_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr conveyor_ctrl conveyor_ctrl_v{};

int use_val (const int &x) conveyor
  pre<conveyor_ctrl_v>(true)
{
  return x;
}

int forward (int &y)
  pre<conveyor_ctrl_v>(std::is_object_address (&y))
  pre<conveyor_ctrl_v>(use_val (y) >= 0)
{ return y; }

int main ()
{
  int ok = 1;
  for (int i = 0; i < 200; ++i)
    {
      int v = 1;
      if (forward (v) != 1)
	ok = 0;
    }
  return ok ? 0 : 1;
}
