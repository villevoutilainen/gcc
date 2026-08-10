// D4324: the built-in GIMPLE-pass engine's own branch-merge boundary
// for the pointer-aliasing fix -- a real two-predecessor CFG join (an
// ordinary if/else, not desugared away at this post-SSA stage) leaves
// q's own def a GIMPLE_PHI with one argument aliasing p and the other
// not. cg_gimple_object_identity's own extended PHI handling only
// returns a shared identity if *every* incoming argument agrees (an
// AND across arms, mirroring cg_provable_object_address_p's own
// identical PHI recursion) -- so post-merge, q correctly has no single
// resolvable identity, the same sound "unknown, not wrongly proven"
// answer the AST engine's own agreement-based alias_merge_with gives
// for the identical source shape (d4324-symbolic-proof-predicate-
// alias-branch-merge-unknown.C). See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs-gimple" }

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
  file f, g;
  file *p = &f;
  file *q = &g;
  open_it (p);
  if (argc)
    q = p;
  use_it (q); // { dg-warning "cannot verify" }
  return 0;
}
