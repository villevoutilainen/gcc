// D4324/P2680: '&p->f''s own is_object_address provability must actually
// consult whether P ITSELF is a proven object address, not just whether
// P's own storage location (trivially, always) is one. Found via direct
// testing: an earlier revision of oa_provable_p's COMPONENT_REF-under-
// ADDR_EXPR case recursed by rebuilding a fresh 'ADDR_EXPR (p)' and
// landed on the "address of a bare decl is trivially provable" shortcut
// -- the wrong question for a pointer dereference, since that shortcut
// is about the pointer VARIABLE's own storage (always live), not about
// what the pointer's VALUE points to.
//
// In practice this was always masked by a separate, independent check
// (item 8's mandatory "pointer dereference not provably valid" scan,
// which calls oa_provable_p directly on the pointer, unaffected by this
// bug) whenever the dereference also appears as ordinary code -- see the
// companion -bad.C, which still shows both diagnostics together. This
// file demonstrates the actually-relevant positive case: once P is
// properly established, both checks agree it's fine.
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

int use_int_mut (int& x) conveyor { return x; }

// P is explicitly proven here, so '&p->v' is now correctly provable too.
int
deref_field_proven (T* p) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (p))
{
  return use_int_mut (p->v);
}

int
main ()
{
  T t{42};
  return deref_field_proven (&t) - 42;
}
