// D4324/Stage 2a: companion to d4324-symbolic-proof-predicate-field-
// alias-invalidated-unknown.C with no intervening mutation -- confirms
// establish-then-consult *through* a struct field slot actually works
// (oa_field_slot_identity resolving on the consult side), not merely
// that Rule 2 invalidation reaches it. See .claude/plans/well-we-last-
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
struct holder { file *ptr; };

void open_it (file * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

int main ()
{
  file f;
  file *p = &f;
  holder h;
  h.ptr = p;
  open_it (p);
  use_it (h.ptr); // must silently discharge, no diagnostic
  return 0;
}
