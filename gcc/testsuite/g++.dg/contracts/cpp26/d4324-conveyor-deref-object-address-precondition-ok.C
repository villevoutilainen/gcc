// D4324/P2680 item 8, Increment W2: the legitimate escape hatch --
// dereferencing a raw pointer parameter is fine once a conveyor-
// flavored precondition has proven std::is_object_address(p) for it,
// established as a separate, earlier 'pre<>' (a later conjunct in the
// very same condition does not benefit from an earlier one's own
// fact). See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

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

int deref (int* p) conveyor pre<conveyor_ctrl_v> (std::is_object_address (p))
{
  return *p;
}

int main () { int x = 5; return deref (&x) - 5; }
