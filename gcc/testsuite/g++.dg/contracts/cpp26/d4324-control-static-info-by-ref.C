// D4324: a control type's assertion_static_info-taking query members
// (is_ignored/constify/assumable/...) may take that parameter by
// const reference instead of by value -- contract_control_bool_member
// must strip the reference before comparing/using the parameter's
// type, or it hits the exact same class of bug as
// d4324-control-wrong-static-info-param.C (TYPE_FIELDS on a
// REFERENCE_TYPE instead of assertion_static_info's own class type,
// rather than gracefully treating it as unusable).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=ignore" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int calls = 0;

struct ref_probe {
  static constexpr bool
  is_ignored (const sc::assertion_static_info& si) noexcept
  { return si.semantic () == sc::evaluation_semantic::ignore; }

  static constexpr bool
  constify (const sc::assertion_static_info&) noexcept { return false; }
  static constexpr bool
  assumable (const sc::assertion_static_info&) noexcept { return false; }

  void
  operator() (const sc::assertion_context& ctx) const
  {
    calls++;
    if (!ctx.check ())
      __builtin_abort ();
  }
};

inline constexpr ref_probe ref_probe_v{};

int f (int x) pre<ref_probe_v>(x >= 0) { return x; }

int main ()
{
  // Under -fcontract-evaluation-semantic=ignore, is_ignored's own
  // by-reference query correctly recognizes the semantic and silences
  // the check entirely -- confirming the reference case is not merely
  // "not crashing" but genuinely usable, unlike the wrong-parameter-
  // type case (d4324-control-wrong-static-info-param.C), which is
  // never usable and so stays unconditionally active.
  int r = f (-1);
  if (r != -1)
    __builtin_abort ();
  if (calls != 0)
    __builtin_abort ();
  return 0;
}
