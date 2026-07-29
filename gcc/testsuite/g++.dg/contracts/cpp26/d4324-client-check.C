// D4324: a named control object is honored by P2900's existing caller-side
// (client) checking mechanism (-fcontracts-client-check=), not just by the
// definition-side check. The caller-side "contract wrapper" function built
// for f (see get_or_create_contract_wrapper_function/copy_and_remap_contracts
// in gcc/cp/contracts.cc) gets a plain, shallow copy of f's own contract
// specifiers -- control-object annotation included -- and that copy then
// goes through the very same build_contract_check dispatch as any other
// function's contracts, with no special-casing needed for control objects
// in the caller-side machinery at all.
//
// -fcontracts-definition-check=off isolates this: f's own definition gets
// no check at all, so the control object can only ever be called from the
// caller-side wrapper, never from f itself.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontracts-client-check=all -fcontracts-definition-check=off" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

bool pre_called = false;
bool post_called = false;

struct probe {
  static constexpr bool is_ignored (sc::evaluation_semantic) { return false; }
  static constexpr bool constify (sc::evaluation_semantic) { return false; }
  static constexpr bool assumable (sc::evaluation_semantic) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    if (ctx.kind () == sc::assertion_kind::pre)
      pre_called = true;
    else if (ctx.kind () == sc::assertion_kind::post)
      post_called = true;
    if (!ctx.check ())
      __builtin_abort ();		// the predicates here always hold
  }
};

inline constexpr probe probe_v{};

// Declaration and definition are deliberately separate: the caller-side
// wrapper is built from the declaration seen at the call site in main(),
// while f's own body (no embedded check, since definition-side checking
// is off here) is defined afterward.
int f (int x) pre<probe_v>(x >= 0) post<probe_v>(r: r >= 0);

int
f (int x)
{
  return x;
}

int main ()
{
  if (f (1) != 1)
    __builtin_abort ();
  if (!pre_called || !post_called)
    __builtin_abort ();
  return 0;
}
