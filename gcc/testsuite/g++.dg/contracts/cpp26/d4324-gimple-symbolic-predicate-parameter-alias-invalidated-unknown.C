// D4324/Stage 4e: the GIMPLE analogue of d4324-symbolic-proof-
// predicate-parameter-alias-invalidated-unknown.C (Stage 3's own
// AST-engine test) -- two of a function's own distinct parameters are
// never treated as potentially the same object, even though 'f(p, p)'
// is ordinary, legal C++ unless a parameter is __restrict-qualified.
// Ported via cg_invalidate_parameter_alias_group, reusing contracts.
// cc's own oa_could_alias_as_parameters (exported as oa_could_alias_
// as_parameters_public) unchanged -- confirmed it needs no AST-
// specific adjustment, since it only ever inspects two PARM_DECLs' own
// types. Key representation difference from the AST port: an identity
// flowing through this engine's own fact maps for a by-value pointer
// parameter is that parameter's own default-def SSA name (ssa_
// default_def), not the bare PARM_DECL, confirmed via cg_gimple_
// object_identity_1's own SSA_NAME branch and cg_seed_predicate_self_
// trust's own identical lookup. See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
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

void open_it (file * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void mutate_via_alias (file *f) { f->opened = false; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

void g (file *a, file *b)
{
  open_it (a);
  mutate_via_alias (b);
  use_it (a); // { dg-warning "cannot verify" }
}

int main ()
{
  file f1, f2;
  g (&f1, &f2);
  return 0;
}
