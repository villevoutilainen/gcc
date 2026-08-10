// D4324: loop-carried case for the pointer-aliasing fix -- q aliases p
// before the loop, but the loop body may reassign q at least once (to
// a different, never-opened object), so q's own value after the loop
// can no longer be trusted to still equal whatever it aliased before
// entering it. oa_handle_loop's own per-reassigned-decl treatment now
// also calls alias_invalidate on every decl reassigned anywhere in the
// loop's repeated part, the same "unconditionally invalidated, no
// sound loop-invariant to compute" treatment already applied to the
// predicate/field-range maps themselves right next to it. use_it (q)
// after the loop must be "cannot verify", regardless of N. See
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

void open_it (file * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

int main (int argc, char **)
{
  file f, other;
  file *p = &f;
  open_it (p);
  file *q = p;
  for (int i = 0; i < argc; ++i)
    q = &other;
  use_it (q); // { dg-warning "cannot verify" }
  return 0;
}
