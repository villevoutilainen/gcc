// D4324/Stage 5: isolates the INDIRECT_REF-unwrap fix in oa_field_
// object_identity on its own -- '&hp->f' (pointer-to-struct base) is
// ADDR_EXPR (COMPONENT_REF (INDIRECT_REF (hp), f)) at this stage,
// confirmed via raw tree dump, one INDIRECT_REF deeper than the plain-
// struct '&h.f' shape (ADDR_EXPR (COMPONENT_REF (h, f)), no
// INDIRECT_REF at all) the basic repro test already exercises. Without
// the unwrap (mirroring oa_field_slot_identity's own identical need for
// 'hp->ptr'), 'hp' itself would never resolve to a base identity at
// all. See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
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
struct holder { file f; };

void open_it (file * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void mutate_via_alias (file *f) { f->opened = false; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

int main ()
{
  holder h;
  holder *hp = &h;
  open_it (&hp->f);
  mutate_via_alias (&hp->f);
  use_it (&hp->f); // { dg-warning "cannot verify" }
  return 0;
}
