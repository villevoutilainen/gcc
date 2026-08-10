// D4324/Stage 2a: the field-alias repro through 'hp->ptr' (pointer-to-
// struct, 'hp' itself never separately aliased) -- isolates the
// INDIRECT_REF-unwrap fix in oa_field_slot_identity on its own,
// independent of the combined case in d4324-symbolic-proof-predicate-
// field-alias-through-aliased-ptr-invalidated-unknown.C. See
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
struct holder { file *ptr; };

void open_it (file * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void mutate_via_alias (file *f) { f->opened = false; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

int main ()
{
  file f;
  file *p = &f;
  holder h;
  holder *hp = &h;
  hp->ptr = p;
  open_it (p);
  mutate_via_alias (hp->ptr);
  use_it (p); // { dg-warning "cannot verify" }
  return 0;
}
