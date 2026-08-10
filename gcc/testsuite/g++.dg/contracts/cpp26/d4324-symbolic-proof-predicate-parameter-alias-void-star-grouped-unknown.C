// D4324/Stage 3: a 'void *' parameter and a 'file *' parameter must
// still group -- 'void *' can legitimately alias any object type (a
// common C-API parameter shape), found as an undisclosed gap during
// this stage's own design review: a 'void *' parameter's own pointee
// is never TYPE_MAIN_VARIANT-equal to any concrete type, so a plain
// type-match comparison alone would never group it with anything.
// Fixed via an explicit VOID_TYPE_P check in oa_could_alias_as_
// parameters. Invalidating via the cast ('(file *) b') also confirms
// oa_invalidation_identity_decl's own existing NOP_EXPR/CONVERT_EXPR
// strip reaches the bare 'void *' PARM_DECL. See .claude/plans/
// well-we-last-discussed-ethereal-duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs" }

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

void g (file *a, void *b)
{
  open_it (a);
  mutate_via_alias ((file *) b);
  use_it (a); // { dg-warning "cannot verify" }
}

int main ()
{
  file f1, f2;
  g (&f1, &f2);
  return 0;
}
