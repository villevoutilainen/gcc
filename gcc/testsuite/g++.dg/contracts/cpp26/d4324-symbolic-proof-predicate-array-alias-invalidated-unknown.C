// D4324/Stage 2b: the same pointer-aliasing soundness gap Stage 2a
// fixed for a struct field ('h.ptr = p;') reproduces one syntactic
// shape further, through a named array of pointers: 'arr[0]' holds the
// same value as 'p', but Rule 2 invalidation ('mutate_via_alias
// (arr[0])') never invalidated anything for it, because
// oa_object_identity_decl/oa_invalidation_identity_decl don't recognize
// ARRAY_REF at all. Fixed via a new, separate resolver, oa_array_slot_
// identity, and a new (base_identity, HOST_WIDE_INT)-keyed alias map,
// m_array_alias_target -- deliberately NOT folded into oa_object_
// identity_decl itself, for the identical Rule-1-dispatch-corruption
// reason Stage 2a's own oa_field_slot_identity resolver isn't either.
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

void open_it (file * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void mutate_via_alias (file *f) { f->opened = false; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

int main ()
{
  file f;
  file *p = &f;
  file *arr[3];
  arr[0] = p;
  open_it (p);
  mutate_via_alias (arr[0]);
  use_it (p); // { dg-warning "cannot verify" }
  return 0;
}
