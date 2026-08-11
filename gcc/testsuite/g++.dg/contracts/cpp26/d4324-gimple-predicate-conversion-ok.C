// D4324: the built-in GIMPLE-pass engine's own axis of the same named-
// predicate conversion-operator lookthrough covered for the AST-walk
// engine by d4324-conveyor-proof-predicate-conversion-ok.C --
// cg_gimple_object_identity now calls cg_resolve_conversion_receiver
// first, and cg_invalidate_persistent_facts_for_call_args now skips a
// call *to* a conversion operator entirely (the GIMPLE-side analogue of
// oa_call_is_conversion_operator_call), for the same reason: without
// that guard, the GIMPLE statement materializing ref's own conversion-
// operator call to reach its identity would itself invalidate that
// identity, in program order, before either statement's own consult
// ever ran. See .claude/plans/well-we-last-discussed-ethereal-
// duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }

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

struct file { bool opened = false; };

bool is_opened (file *f) conveyor { return f->opened; }

struct file_ref {
  file *f;
  operator file* () const { return f; }
};

void open_it (file * const f) post<conveyor_ctrl_v> (is_opened (f)) { f->opened = true; }
void use_it (file * const f) pre<conveyor_ctrl_v> (is_opened (f)) { }

int main ()
{
  file f;
  file_ref ref{&f};
  open_it (ref);
  use_it (ref);
  return 0;
}
