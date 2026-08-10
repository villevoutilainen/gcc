// D4324/Stage 4b: a regression guard confirming the fixed-point RPO
// dataflow's own merge doesn't spuriously invalidate anything -- if
// *neither* arm of an if/else touches the tracked object at all, the
// fact must still be provably true after the join. See .claude/plans/
// well-we-last-discussed-ethereal-duckling.md.
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

void neither_arm (file *f, bool cond)
{
  open_it (f);
  if (cond)
    { }
  else
    { }
  use_it (f); // must silently discharge, no diagnostic
}

int main ()
{
  file f;
  neither_arm (&f, true);
  return 0;
}
