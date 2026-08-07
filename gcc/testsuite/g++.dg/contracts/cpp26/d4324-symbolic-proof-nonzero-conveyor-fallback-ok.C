// D4324: the allowed one-way trust direction (conveyor feeding symbolic)
// for the *new* nonzero checking -- g's own precondition "n != 0" is
// CONVEYOR-flavored, so it self-trusts into the classic, conveyor-only
// m_nz_map (mandatory, regardless of whether -fcontract-conveyor-proofs
// is even enabled -- only -fcontract-symbolic-proofs is passed below).
// consumer()'s own precondition is SYMBOLIC-flavored; its own obligation
// check first tries oa_provably_nonzero_p (which reads m_nz_map) before
// ever consulting the new symbolic-only map, so it finds g's own
// conveyor-established fact and is discharged silently.  See
// .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs" }

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

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

int consumer (int n) pre<symbolic_ctrl_v>(n != 0) { return 10 / n; }

int g (int n) pre<conveyor_ctrl_v>(n != 0)
{
  return consumer (n);
}

int main ()
{
  return g (5) - 2;
}
