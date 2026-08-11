// D4324/Stage 5: conveyor-flavor counterpart of the basic '&h.f' repro
// (d4324-symbolic-proof-predicate-field-object-invalidated-unknown.C),
// mirroring how every prior stage in this plan duplicates its own core
// test across flavors -- m_predicate_fact_map is shared substrate
// either way. Here the real control-object check still runs at runtime
// regardless of the static prover's own conclusion, so the pre-fix
// version of this bug was "only" a missed diagnostic, not a silent
// runtime failure -- but a missed diagnostic is still the wrong answer.
// MUTATE_VIA_ALIAS deliberately leaves OPENED true (rather than really
// flipping it back to false) so the real runtime check never traps --
// this test is isolating the *static* prover's own missed diagnostic,
// not exercising an actual runtime contract violation. See
// .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

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
bool is_opened (const file *f) conveyor { return f->opened; }
struct holder { file f; };

void open_it (file * const f) post<conveyor_ctrl_v> (is_opened (f)) { f->opened = true; }
void mutate_via_alias (file *f) { f->opened = true; }
void use_it (file * const f) pre<conveyor_ctrl_v> (is_opened (f)) { }

int main ()
{
  holder h;
  open_it (&h.f);
  mutate_via_alias (&h.f);
  use_it (&h.f); // { dg-warning "cannot verify" }
  return 0;
}
