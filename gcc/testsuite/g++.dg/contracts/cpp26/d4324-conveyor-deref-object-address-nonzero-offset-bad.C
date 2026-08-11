// D4324/P2680 item 8, Increment W2: is_object_address(p) proves only
// that 'p' itself denotes a valid object -- it says nothing about
// 'p + n' for an unprovable 'n', so dereferencing through an added,
// unprovable offset still correctly errors even though 'p' alone is
// proven. Confirms the is_object_address fallback doesn't overreach.
// See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do compile { target c++26 } }
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

int deref_offset (int* p, int n) conveyor pre<conveyor_ctrl_v> (std::is_object_address (p))
{
  return *(p + n); // { dg-error "pointer dereference of .*not provably valid" }
}

int main () { int x = 5; return deref_offset (&x, 0); }
