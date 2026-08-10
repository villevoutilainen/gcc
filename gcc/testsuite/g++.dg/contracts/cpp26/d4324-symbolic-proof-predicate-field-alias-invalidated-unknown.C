// D4324/Stage 2a: the same pointer-aliasing soundness gap Stage 1 fixed
// for a bare decl (d4324-symbolic-proof-predicate-alias-invalidated-
// unknown.C) reproduces through a struct/class field slot too: 'h.ptr'
// holds the same value as 'p', but Rule 2 invalidation ('mutate_via_
// alias (h.ptr)') never invalidated anything for it, because
// oa_object_identity_decl/oa_invalidation_identity_decl don't recognize
// a COMPONENT_REF at all. Fixed via a new, separate resolver, oa_
// field_slot_identity, and a new (base_identity, FIELD_DECL)-keyed
// alias map, m_field_alias_target -- deliberately NOT folded into
// oa_object_identity_decl itself, since that function's own true/false
// return is used as a control-flow discriminator at Rule 1's own
// reassignment dispatch (see d4324-symbolic-proof-predicate-field-
// write-not-corrupting-other-alias-ok.C for the regression that design
// mistake would have caused). See .claude/plans/well-we-last-discussed-
// ethereal-duckling.md.
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
struct holder { file *ptr; };

void open_it (file * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void mutate_via_alias (file *f) { f->opened = false; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

int main ()
{
  file f;
  file *p = &f;
  holder h;
  h.ptr = p;
  open_it (p);
  mutate_via_alias (h.ptr);
  use_it (p); // { dg-warning "cannot verify" }
  return 0;
}
