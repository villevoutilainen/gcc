// D4324: oa_refine_single_comparison, the shared primitive behind every
// runtime if/ternary condition's own fact refinement, now also
// recognizes a ptr->field conjunct (not just a bare decl) -- a real
// gap found while building the call-range analogue (item 7): field
// ranges were previously only ever established from a *declared*
// pre<>/post<>, never from an ordinary runtime 'if (ptr->field < N)'
// check, unlike a bare decl.
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
  int count;
  int get () const conveyor pre<conveyor_ctrl_v>(count > 3) { return 1; }
};

int use_checked (S& s) conveyor
{
  if (s.count > 3)
    return s.get ();
  return -1;
}

int use_unchecked (S& s) conveyor
{
  return s.get (); // { dg-warning "cannot verify that field .S::count." }
}

int main () { S s; s.count = 5; return use_checked (s) + use_unchecked (s); }
