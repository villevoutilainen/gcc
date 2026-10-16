// D4324 + templates: declaration-level contracts on a plain function
// pointer that is itself a FUNCTION TEMPLATE's own parameter (as
// opposed to a variable template) -- see d4324-object-decl-template-
// fnptr.C for the variable-template case. Also covers a postcondition
// with a result identifier on a dependent return type, instantiated
// at two different types.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

struct labeled {
  const char* label;
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  { if (ctx.check ()) return; seen = label; }
  static const char* seen;
};
const char* labeled::seen = nullptr;

inline constexpr labeled post_ctrl{"post violated"};

template <typename R>
R
invoke (R (*pf) (R x) post<post_ctrl> (r: r >= 0), R x)
{
  return pf (x);
}

int identity_int (int x) { return x; }
double negate_double (double x) { return -x; } // violates for a positive x

int
main ()
{
  if (invoke (identity_int, 5) != 5 || labeled::seen)
    __builtin_abort ();

  if (invoke (negate_double, -3.0) != 3.0 || labeled::seen)
    __builtin_abort ();

  invoke (negate_double, 2.0); // result -2.0 violates r >= 0
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "post violated") != 0)
    __builtin_abort ();

  return 0;
}
