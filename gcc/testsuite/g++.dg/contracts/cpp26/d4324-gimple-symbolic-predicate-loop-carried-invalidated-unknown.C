// D4324/Stage 4b: confirms the fixed-point RPO dataflow correctly
// propagates an invalidation from inside a loop body back through the
// loop header to code after the loop -- an initial, rejected single-
// pass RPO design (never shipped, caught by review before
// implementation) would have missed this: on a single forward pass,
// the loop header's own successors are computed from the header's
// state *before* the loop body (reachable from the header only via a
// not-yet-processed back edge) ever runs, so an invalidation genuinely
// true on *every* real execution path through the loop would have
// been silently lost -- exactly the same class of unsoundness the
// dom-walker bug itself had, just relocated from "one arm of an if" to
// "inside any loop". The shipped fixed-point iteration (repeatedly
// recomputing every block until nothing changes, mirroring tree-ssa-
// pre.cc's own identical shape) closes this. See .claude/plans/well-
// we-last-discussed-ethereal-duckling.md.
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

void loop_carried (file *f, int n)
{
  open_it (f);
  for (int i = 0; i < n; ++i)
    mutate_via_alias (f);
  use_it (f); // { dg-warning "cannot verify" }
}

int main ()
{
  file f;
  loop_carried (&f, 3);
  return 0;
}
