// D4324/Stage 2a: the combined case that broke the first draft of
// oa_field_slot_identity twice over -- 'hp' is itself reassigned
// (Stage 1's own m_alias_target mechanism), and 'hp->ptr' is then
// written and later read through it. Requires both the INDIRECT_REF
// unwrap AND the read-side env.alias_find canonicalization of the base
// before the field-map lookup (mirroring the write side) to compose
// correctly. See .claude/plans/well-we-last-discussed-ethereal-
// duckling.md.
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
  holder h1, h2;
  holder *hp = &h1;
  hp = &h2;
  hp->ptr = p;
  open_it (p);
  mutate_via_alias (hp->ptr);
  use_it (p); // { dg-warning "cannot verify" }
  return 0;
}
