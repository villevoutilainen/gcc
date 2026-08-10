// D4324: branch-merge boundary for the pointer-aliasing fix -- q only
// aliases p on the 'if' arm actually taken here; the *other* arm
// leaves q pointing at its own, never-opened object. oa_env::alias_
// merge_with is agreement-based (mirrors predicate_fact_merge_with's
// own "keep only if both sides agree" rule), not a union, specifically
// so an alias created on only one arm does not survive unconditionally
// past the join -- the rejected DSU design's own union-only merge
// would have failed exactly this case by construction. use_it (q)
// after the join must still be "cannot verify", not wrongly proven,
// regardless of which arm actually ran at runtime. See .claude/plans/
// well-we-last-discussed-ethereal-duckling.md.
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
  file f, g;
  file *p = &f;
  file *q = &g;
  open_it (p);
  if (argc)
    q = p;
  use_it (q); // { dg-warning "cannot verify" }
  return 0;
}
