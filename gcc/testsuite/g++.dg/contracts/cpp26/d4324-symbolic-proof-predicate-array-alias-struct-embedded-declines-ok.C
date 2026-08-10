// D4324/Stage 2b: struct/class-embedded arrays ('h2.arr[0]',
// 'hp2->arr[0]') are an explicitly documented, out-of-scope residual
// gap, not something this stage claims to fix -- confirmed via a raw
// tree dump that in both cases the ARRAY_REF's own base
// (TREE_OPERAND (ARRAY_REF, 0)) is itself a COMPONENT_REF ('h2.arr'/
// 'hp2->arr'), which oa_object_identity_decl can't resolve (no
// COMPONENT_REF case), so array_identity resolution fails identically
// on both the write and read sides -- this genuinely, silently
// reproduces the original motivating bug for this one shape, not a
// "gracefully declines and stays sound" case the way the nested-field
// gap (Stage 2a) or the deeper array-of-structs gap are. Properly
// supporting it would need a fundamentally different 3-tuple
// (struct_identity, FIELD_DECL, index) key, not attempted here. This
// test documents the current, honestly-disclosed behavior (silent
// discharge, no diagnostic) so a future fix's own test can compare
// against it, the same spirit as Stage 2a's own nested-declines-ok.C.
// See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
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
struct holder2 { file *arr[3]; };

void open_it (file * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void mutate_via_alias (file *f) { f->opened = false; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

int main ()
{
  file f;
  file *p = &f;
  holder2 h2;
  h2.arr[0] = p;
  open_it (p);
  mutate_via_alias (h2.arr[0]);
  use_it (p); // known gap: silently discharges, no diagnostic
  return 0;
}
