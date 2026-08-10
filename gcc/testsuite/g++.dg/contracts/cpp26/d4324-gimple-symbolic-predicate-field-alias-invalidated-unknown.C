// D4324/Stage 4d: the GIMPLE analogue of d4324-symbolic-proof-
// predicate-field-alias-invalidated-unknown.C (Stage 2a's own AST-
// engine test) -- 'h->ptr' holds the same value as 'p', but Rule 2
// invalidation for 'mutate_via_alias (h->ptr)' never invalidated
// anything for it before this stage. Found and fixed during
// implementation that unlike the AST engine (where 'h->ptr' can
// appear directly as a call-argument expression), a GIMPLE call
// argument must itself be an SSA value -- confirmed via raw dump that
// 'mutate (h->ptr);' lowers to '_1 = h->ptr; mutate (_1);' -- so
// cg_field_slot_identity must chase through the SSA temporary's own
// def-stmt first. Also found and fixed: cg_field_slot_identity must
// be tried *before* cg_gimple_object_identity at every call-boundary
// site, not as a fallback-only-if-null -- unlike the AST engine's own
// oa_object_identity_decl (which genuinely declines for anything it
// doesn't recognize), cg_gimple_object_identity always "succeeds" for
// any pointer/reference-typed SSA value, falling back to the value
// itself, which would silently pre-empt the more specific resolver
// from ever running if tried second. See .claude/plans/well-we-last-
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
struct holder { file *ptr; };

void open_it (file * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void mutate_via_alias (file *f) { f->opened = false; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

void g (file *p, holder *h)
{
  h->ptr = p;
  open_it (p);
  mutate_via_alias (h->ptr);
  use_it (p); // { dg-warning "cannot verify" }
}

int main ()
{
  file f;
  holder h;
  g (&f, &h);
  return 0;
}
