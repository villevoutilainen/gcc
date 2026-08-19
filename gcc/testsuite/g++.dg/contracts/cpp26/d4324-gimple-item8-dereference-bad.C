// Companion to d4324-gimple-item8-dereference-ok.C: unconstrained
// pointer parameters, dereferenced (as a value read and as a field
// access). Rejected by contracts.cc's own mandatory, unconditional item
// 8 pass regardless of any GIMPLE flag (confirmed by direct testing:
// the identical errors fire even with -fcontract-conveyor-proofs-gimple
// entirely absent) -- so, exactly like item 8's own div/mod/overflow
// violation tests, this can only demonstrate "compilation still
// correctly rejects this with both engines active," not GIMPLE's own
// check in isolation.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }
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

int deref_bad (int *p) conveyor
{
  return *p; // { dg-error "not provably valid" }
}

/* A field access through a pointer ('p->v') compiles to a GIMPLE
   COMPONENT_REF (MEM_REF (...), field) whose own carrying statement has
   lost its precise source location by this pass point (confirmed by
   direct testing: gimple_location for it reports the function's own
   closing brace, not the 'return' line -- the same class of location
   imprecision item 7's own d4324-gimple-item7-ownership-bad.C avoids by
   using a call-site error instead) -- the dg-error below is therefore
   anchored to the closing brace, matching where the diagnostic actually
   lands, not to the 'return' statement itself.  */
struct T { int v; };
int deref_field_bad (T *p) conveyor
{
  return p->v;
} // { dg-error "not provably valid" }

int main () { return 0; }
