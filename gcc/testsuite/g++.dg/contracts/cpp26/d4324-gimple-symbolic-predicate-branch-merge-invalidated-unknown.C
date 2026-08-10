// D4324/Stage 4b: a severe, pre-existing bug in the GIMPLE-pass
// engine's own dominator-tree walk, found while investigating how to
// add a Rule 1 equivalent -- entirely independent of aliasing.
// cg_predicate_dom_walker (the shipped-before-this-stage class)
// computed each block's own inherited state purely from its immediate
// dominator's own output, never from its actual CFG predecessors; for
// an ordinary if/else with no early return, the join block is a
// *sibling* of both arms in the dominator tree (all three are direct
// children of the header), not a descendant of either, so a fact
// invalidated on only one arm silently survived past the join.
// Replaced with a genuine fixed-point dataflow (cg_predicate_facts_
// walk) over a reverse-postorder block order, mirroring tree-ssa-
// pre.cc's own identical shape. See .claude/plans/well-we-last-
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
void mutate_via_alias (file *f) { f->opened = false; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

void join_caller (file *f, bool cond)
{
  open_it (f);
  if (cond)
    mutate_via_alias (f);
  use_it (f); // { dg-warning "cannot verify" }
}

int main ()
{
  file f;
  join_caller (&f, true);
  return 0;
}
