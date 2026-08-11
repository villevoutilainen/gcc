// D4324/Stage 5: an unrelated field of the SAME struct instance must
// not be disturbed -- 'h.f1' and 'h.f2' resolve to different
// (base_identity, FIELD_DECL) pairs, and therefore different cached
// placeholder keys, so establishing/invalidating one must never touch
// the other's own predicate fact. See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
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
struct holder { file f1; file f2; };

void open_it (file * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void mutate_via_alias (file *f) { f->opened = false; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

int main ()
{
  holder h;
  open_it (&h.f1);
  mutate_via_alias (&h.f2); // never opened -- unrelated field
  use_it (&h.f1); // must silently discharge, no diagnostic
  return 0;
}
