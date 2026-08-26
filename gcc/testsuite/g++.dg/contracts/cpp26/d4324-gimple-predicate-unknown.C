// The built-in GIMPLE-pass engine's own OA_UNKNOWN case for named
// predicates (-fcontract-conveyor-proofs-gimple): 'p' is a plain,
// unconstrained pointer parameter with no established fact of any
// kind, so there is nothing for read()'s own precondition obligation
// to consult. Predicate facts are purely an opt-in-prover concept (no
// mandatory call-site obligation check exists for them at all, same
// as nonzero/general ranges), so this compiles and runs successfully
// with only this engine's own warning. See gcc/cp/contracts-gimple.cc
// and ~/gimple-contract-analysis.md.
//
// The follow-up dg-message demonstrates the diagnostic-precision work
// (oa_unprovable_reason, contracts.h): this specific "cannot verify"
// case is OA_UNPROVABLE_NO_FACT (nothing at all established about 'p',
// as opposed to a fact existing for a different predicate/object, or
// under weaker trust than required -- see cg_consult_persistent_facts'
// own predicate loop for where all three are now distinguished).
//
// relay's own is_object_address(p) conjunct (2026-08-26, alongside the
// fix making a conveyor-active pre/post on a non-'conveyor'-declared
// function synthesize the same implicit reference/'this'-safety
// precondition a real 'conveyor' function already gets): read()'s own
// 'pre<conveyor_ctrl_v>(is_opened(this))' is conveyor-active even
// though read() itself has no 'conveyor' keyword, so it now correctly
// gets its own synthesized 'is_object_address(this)' obligation too --
// relay must establish that for 'p' explicitly, exactly like any other
// caller of a conveyor-checked function would, or this test would be
// exercising an unsound call (an unproven 'p' dereferenced through
// read()'s own 'this') rather than the OA_UNKNOWN case it's actually
// about.
// { dg-do run }
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

struct io_facility {
  static bool is_opened (io_facility*) conveyor { return true; }
  void read () pre<conveyor_ctrl_v>(is_opened (this)) {}
};

void relay (io_facility *p) pre<conveyor_ctrl_v>(std::is_object_address (p))
{
  p->read (); // { dg-warning "cannot verify that .*is_opened.*holds" }
              // { dg-message "no fact relating this value" "unprovable reason" { target *-*-* } .-1 }
}

int main () { io_facility f; relay (&f); return 0; }
