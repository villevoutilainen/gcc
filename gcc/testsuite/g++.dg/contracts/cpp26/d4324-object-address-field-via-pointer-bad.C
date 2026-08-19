// D4324/P2680: companion to the -ok.C case. P is never established here,
// so BOTH item 7's implicit is_object_address obligation (for '&p->v'
// bound to use_int_mut's reference parameter) and item 8's independent
// mandatory dereference-validity scan (which calls oa_provable_p on P
// directly, unaffected by the bug -ok.C's own comment describes) must
// reject it. Before the fix, only the second of these two fired; the
// first was silently, wrongly treated as trivially provable, since
// oa_provable_p's own COMPONENT_REF-under-ADDR_EXPR case answered "is P's
// own storage live" (always yes) instead of "is P's own VALUE a proven
// address" (env.provable_p (p), the real check).
// { dg-do compile { target c++26 } }
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

int use_int_mut (int& x) conveyor { return x; }

int
deref_field_unproven (T* p) conveyor
{
  return use_int_mut (p->v); // { dg-error "cannot prove .is_object_address. for .p->T::v." }
} // { dg-error "pointer dereference of .p. not provably valid" }

int main () { return 0; }
