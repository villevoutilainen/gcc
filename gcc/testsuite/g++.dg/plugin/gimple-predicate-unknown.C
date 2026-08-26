// gimple_object_address_plugin.cc: named predicate's own OA_UNKNOWN
// case -- 'p' is a plain, unconstrained pointer parameter with no
// established fact of any kind, so there is nothing for read()'s own
// precondition obligation to consult. Predicate facts are purely an
// opt-in-prover concept in the real engine (no mandatory call-site
// obligation check exists for them at all, same as nonzero/general
// ranges), so this compiles and runs successfully with only this
// prototype's own warning. See ~/gimple-contract-analysis.md.
//
// relay's own is_object_address(p) conjunct (2026-08-26, alongside the
// fix making a conveyor-active pre/post on a non-'conveyor'-declared
// function synthesize the same implicit reference/'this'-safety
// precondition a real 'conveyor' function already gets -- see the
// identical fix to the built-in engine's own d4324-gimple-predicate-
// unknown.C): read()'s own 'pre<conveyor_ctrl_v>(is_opened(this))' is
// conveyor-active even though read() itself has no 'conveyor' keyword,
// so it now correctly gets its own synthesized 'is_object_address
// (this)' obligation too -- relay must establish that for 'p'
// explicitly, or this test would be exercising an unsound call rather
// than the OA_UNKNOWN case it's actually about.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
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
  p->read (); // { dg-warning "gimple-oa: cannot verify" }
}

int main () { io_facility f; relay (&f); return 0; }
