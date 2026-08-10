// D4324: the built-in GIMPLE-pass engine's own axis of the same
// pointer-aliasing gap fixed on the AST-walk engine by d4324-symbolic-
// proof-predicate-alias-invalidated-unknown.C -- a bare pointer-typed
// SSA_NAME used to be its own identity unconditionally, so 'q_2 = p_1;'
// left q_2 and p_1 as two different identities even though they hold
// the same value. Fixed by extending cg_gimple_object_identity to
// chase a plain copy/conversion through its own def-stmt, mirroring
// cg_provable_object_address_p's own identical SSA_NAME chasing. See
// .claude/plans/well-we-last-discussed-ethereal-duckling.md.
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

int main ()
{
  file f;
  file *p = &f;
  open_it (p);
  file *q = p;
  mutate_via_alias (q);
  use_it (p); // { dg-warning "cannot verify" }
  return 0;
}
