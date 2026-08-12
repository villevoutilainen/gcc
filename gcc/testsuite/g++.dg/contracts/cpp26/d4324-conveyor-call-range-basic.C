// D4324: -fcontract-conveyor-proofs' call-range shape ("RECEIVER.
// ACCESSOR () OP literal", e.g. 'v.size () > 3') -- the call analogue
// of the existing ptr->field range shape, for a call to a
// DECL_DECLARED_CONVEYOR_P accessor rather than a field access. An
// established fact (here, from an ordinary runtime if-condition --
// see oa_refine_single_comparison's own call-range branch) is silently
// discharged; no established fact is reported as unverifiable.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

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

struct S {
  int size () const conveyor { return 5; }
  int get () const conveyor pre<conveyor_ctrl_v>(size () > 3) { return 1; }
};

int use_checked (S& s) conveyor
{
  if (s.size () > 3)
    return s.get ();
  return -1;
}

int use_unchecked (S& s) conveyor
{
  return s.get (); // { dg-warning "cannot verify that .int S::size\\(\\) const. called on .s." }
}

int main () { S s; return use_checked (s) + use_unchecked (s); }
