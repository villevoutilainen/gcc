// D4324/Stage 5: the GIMPLE analogue of d4324-symbolic-proof-predicate-
// field-object-invalidated-unknown.C (Stage 5's own AST-engine test) --
// '&h->f' (the address of a non-pointer, embedded sub-object) was never
// recognized as an identity-bearing expression on this engine either.
// Fixed via cg_field_object_identity, the GIMPLE analogue of
// oa_field_object_identity, plugged into the *existing* state.pred map
// via a synthesized, cached placeholder tree per (base_identity,
// FIELD_DECL) pair -- same design as the AST engine, but the cache
// itself is a plain local hash_map inside cg_predicate_facts_walk
// (shared across both its fixed-point and final consult phases), not a
// pointer field needing copy()/assign() propagation the way the AST
// engine's own oa_env::m_field_object_key does, since this engine's
// state is already threaded by reference through one single, function-
// scoped walk with no forking oa_env analogue at all.
//
// Also re-confirmed (not assumed to carry over from Stage 4d unchanged)
// that '&h->f' used as a call argument always lowers through an SSA
// temporary first ('_1 = &h_2(D)->f; mutate (_1);'), needing the same
// SSA-chase cg_field_slot_identity already required, and that this
// resolver must be tried *before* cg_gimple_object_identity at every
// call site (which always "succeeds," falling back to the SSA value
// itself, for a def-stmt it doesn't otherwise recognize -- confirmed
// this includes an ADDR_EXPR def-stmt). See .claude/plans/well-we-
// last-discussed-ethereal-duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs-gimple" }

#include <contracts>
namespace sc = std::contracts;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

struct file { bool opened = false; };
bool is_opened (const file *f) symbolic;
struct holder { file f; };

void open_it (file * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void mutate_via_alias (file *f) { f->opened = false; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

void g (holder *h)
{
  open_it (&h->f);
  mutate_via_alias (&h->f);
  use_it (&h->f); // { dg-warning "cannot verify" }
}

int main ()
{
  holder h;
  g (&h);
  return 0;
}
