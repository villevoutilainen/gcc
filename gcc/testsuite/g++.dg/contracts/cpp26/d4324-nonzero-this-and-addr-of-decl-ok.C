// D4324/P2680: oa_provably_nonzero_p was missing the 'this' and
// '&decl' base cases oa_provable_p has always treated as axioms --
// neither a live 'this' receiver nor the address of a real variable/
// parameter can ever be a null pointer, so both should be trivially
// provable for a conveyor callee's 'pre (p != 0)'-style obligation, the
// same way they're already trivially provable for is_object_address.
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

struct T { int v; };

struct S {
  int use_nonnull (S* p) conveyor pre<conveyor_ctrl_v>(p != nullptr) { return 1; }
  int self_call () conveyor { return use_nonnull (this); }
};

int use_nonnull (T* p) conveyor pre<conveyor_ctrl_v>(p != nullptr) { return 1; }

int
accept_addr_of_local () conveyor
{
  T x{7};
  return use_nonnull (&x);
}

int
main ()
{
  S s;
  return s.self_call () + accept_addr_of_local () - 2;
}
