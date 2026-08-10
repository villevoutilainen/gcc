// D4324: soundness-boundary regression guard for the pointer-aliasing
// fix (d4324-symbolic-proof-predicate-alias-invalidated-unknown.C) --
// locks in the failure mode of a *rejected* earlier design (a
// persistent union-find/DSU, merged across branches by unioning
// edges). DSU's own union(a,b) operates on roots, so reassigning any
// variable that once shared history with a component permanently
// fuses that whole component with whatever it's reassigned to next:
// 'scratch = p1; scratch = p2;' would incorrectly treat p1 and p2 as
// forever the same identity, even though scratch never simultaneously
// aliased both -- p1 and p2 are two genuinely unrelated objects here.
// The final design (a plain, overwritten hash_map, see oa_env::
// alias_find's own comment) avoids this: reassigning SCRATCH only
// touches SCRATCH's own entry. is_opened(p2) must not leak onto p1.
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
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

int main ()
{
  file f1, f2;
  file *p1 = &f1;
  file *p2 = &f2;
  file *scratch = p1;
  scratch = p2;
  open_it (p2);
  use_it (p1); // { dg-warning "cannot verify" }
  return 0;
}
