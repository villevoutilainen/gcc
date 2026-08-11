// D4324/Stage 5: '&h.f' (the address of a struct member, here a
// non-pointer, embedded sub-object) was never recognized as an
// identity-bearing expression at all -- neither oa_object_identity_decl
// (only accepts '&decl' for a bare VAR_DECL/PARM_DECL operand, never
// '&COMPONENT_REF') nor oa_field_slot_identity (an alias-VALUE lookup
// for a pointer-typed field slot, a different question entirely from
// "what is the address of this sub-object") could resolve it, so
// 'is_opened (&h.f)' was silently never established nor consulted, with
// zero diagnostic regardless of any intervening mutation -- confirmed
// via direct testing to reproduce identically on both engines. Fixed
// via a new resolver, oa_field_object_identity, and a synthesized,
// cached, stable placeholder tree per (base_identity, FIELD_DECL) pair
// (oa_env::field_object_identity_key), plugged into the *existing*
// m_predicate_fact_map rather than a new parallel map. See
// .claude/plans/well-we-last-discussed-ethereal-duckling.md.
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
  open_it (&h.f);
  mutate_via_alias (&h.f);
  use_it (&h.f); // { dg-warning "cannot verify" }
  return 0;
}
