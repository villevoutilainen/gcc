// D4324/Stage 2a: guards the field-type gate on the write side --
// writing to a non-pointer field of the same struct ('h.count') must
// not populate m_field_alias_target at all (Stage 2a's field-alias
// detection is gated on the field's own type, mirroring Stage 1's own
// plain-decl gating). Confirms this doesn't spuriously interact with
// the pointer field's own, independently-tracked alias. See
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
struct holder { file *ptr; int count; };

void open_it (file * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

int main ()
{
  file f;
  file *p = &f;
  int q = 42;
  holder h;
  h.ptr = p;
  open_it (p);
  h.count = q; // non-pointer field write, must not touch h.ptr's alias
  use_it (h.ptr); // must silently discharge, no diagnostic
  return 0;
}
