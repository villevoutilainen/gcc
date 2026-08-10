// D4324/Stage 2a: regression test for the rejected first draft's
// severe bug -- teaching oa_object_identity_decl itself to resolve a
// COMPONENT_REF would have made a later, UNRELATED write to 'h.ptr'
// wrongly take Rule 1's whole-object dispatch branch, invalidating
// whatever 'h.ptr' used to alias instead of doing the correct,
// narrower per-field invalidation. The corrected design (a separate,
// additive oa_field_slot_identity resolver, never touching
// oa_object_identity_decl) must not exhibit this: 'p' keeps its own,
// independently-established fact untouched by an unrelated later
// field write. See .claude/plans/well-we-last-discussed-ethereal-
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
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

int main ()
{
  file f, q;
  file *p = &f;
  holder h;
  h.ptr = p;
  open_it (p);
  h.ptr = &q; // unrelated later write to h.ptr; q was never opened
  use_it (p); // must silently discharge, no diagnostic
  return 0;
}
