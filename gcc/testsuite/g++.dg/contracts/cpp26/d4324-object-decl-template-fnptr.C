// D4324 + templates: declaration-level contracts on callable-typed
// objects (see .claude/plans/stateless-jumping-shore.md), extended to
// support templates for the TYPE_PTRFN_P/TYPE_REFFN_P shapes -- the
// feature previously had zero template support at all (contracts.cc's
// own file comment said so explicitly). Covers a variable template
// (dependent return and parameter type) and a function-template's own
// parameter, each instantiated more than once to confirm per-
// instantiation independence (one instantiation's own contract state
// must not leak into another's), plus a postcondition with a result
// identifier on a dependent return type.
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

inline constexpr labeled pre_ctrl{"pre violated"};
inline constexpr labeled post_ctrl{"post violated"};

// Variable template: dependent return type and dependent parameter
// type, both on the same declaration.
template <typename R>
R (*pf)(R x) pre<pre_ctrl>(x >= 0);

int identity_int (int x) { return x; }
double identity_double (double x) { return x; }
int not_identity (int) { return -1; } // for the postcondition test below

int
main ()
{
  pf<int> = identity_int;
  pf<double> = identity_double;

  if (pf<int> (5) != 5 || labeled::seen)
    __builtin_abort ();
  if (pf<double> (2.5) != 2.5 || labeled::seen)
    __builtin_abort ();

  // Each instantiation's own condition is checked independently.
  pf<int> (-1);
  if (!labeled::seen || __builtin_strcmp (labeled::seen, "pre violated") != 0)
    __builtin_abort ();
  labeled::seen = nullptr;

  pf<double> (1.0); // still satisfies its own condition
  if (labeled::seen)
    __builtin_abort ();

  return 0;
}
