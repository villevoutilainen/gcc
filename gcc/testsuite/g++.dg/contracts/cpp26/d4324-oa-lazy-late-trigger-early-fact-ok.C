// D4324: confirms the real oa_* walk, once DECL_MIGHT_NEED_OA_SCAN_P
// triggers it, still runs from the top of the function and sees an
// early-established fact -- not just from wherever the triggering
// construct happens to be. f has no pre/post/assert/conveyor-
// declaration of its own; under the lazy, FUNCTION_DECL-tagging
// detection design the *only* reason f ever gets walked at all is the
// call to 'deref' at the very end (maybe_contract_wrap_call's own
// touch point), discovered only once parsing reaches that point --
// yet the object-address fact for 'p', established at the very top
// via an ordinary address-of a local, must still be visible by the
// time that late call is reached. Confirmed by direct experiment: an
// otherwise-identical function using an unprovable pointer instead of
// '&a' still correctly produces "cannot prove" here, so this is a
// real, discriminating proof (not something that would trivially pass
// regardless of whether the fact tracking actually worked).
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

int deref (int* p) pre<conveyor_ctrl_v>(std::is_object_address(p))
{
  return *p;
}

int
f (int n)
{
  // Early fact establishment, right at the top.
  int a = 1;
  int* p = &a;

  // Padding/unrelated control flow, putting real distance between the
  // fact and its use.
  int junk = 0;
  for (int i = 0; i < n; ++i)
    junk += i;
  if (junk > 100)
    return junk;

  // The only construct anywhere in this function that makes
  // DECL_MIGHT_NEED_OA_SCAN_P true at all under the new touch-point
  // design (f has no contract_assert/pre/post of its own) -- must
  // still see p's own early-established fact from the very top.
  return deref (p) + junk;
}

int main () { return f (3) - (1 + (0 + 1 + 2)); }
