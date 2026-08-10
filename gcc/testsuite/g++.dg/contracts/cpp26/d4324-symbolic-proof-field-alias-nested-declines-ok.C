// D4324/Stage 2a: nesting through a non-pointer intermediate field
// ('o.holder_field.ptr') is a documented, explicitly out-of-scope
// residual gap, not something this stage claims to fix -- confirmed
// via direct testing this "degrades gracefully" (oa_field_slot_
// identity's own base resolution requires oa_object_identity_decl to
// succeed on the *base* of the outermost COMPONENT_REF, and that
// function has no COMPONENT_REF case at all, so a COMPONENT_REF base
// like 'o.holder_field' itself always fails to resolve) rather than
// mis-handling it (no bogus map entry is ever created, no ICE, no
// wrong identity substituted). Both the write side (Rule 1's own
// COMPONENT_REF block never even reaches field_alias_set, since its
// own 'oa_object_identity_decl (obj_expr, &field_identity)' gate fails
// the same way) and Rule 2's own invalidation attempt decline the same
// way, so 'mutate_via_alias' below is never recognized as touching the
// same object 'p' names -- 'p' own, already-established fact survives
// untouched, and 'use_it (p)' silently discharges with no diagnostic.
// This is the same class of silent gap Stage 1/2a fix for one and two
// levels of indirection respectively, just still open one level
// deeper -- a real limitation, honestly not covered by this stage,
// not a false "safe" claim. See .claude/plans/well-we-last-discussed-
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
struct outer { holder holder_field; };

void open_it (file * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void mutate_via_alias (file *f) { f->opened = false; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

int main ()
{
  file f;
  file *p = &f;
  outer o;
  o.holder_field.ptr = p;
  open_it (p);
  mutate_via_alias (o.holder_field.ptr);
  use_it (p); // known gap: silently discharges, no diagnostic
  return 0;
}
