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

/* A field access through a pointer ('p->v'): both engines report it at
   the statement.  */
struct T { int v; };
int deref_field_bad (T *p) conveyor
{
  return p->v; // { dg-error "not provably valid" }
}

int main () { return 0; }
