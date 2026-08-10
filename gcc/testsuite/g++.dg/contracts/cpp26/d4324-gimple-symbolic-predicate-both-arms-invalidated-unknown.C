// D4324/Stage 4b: a regression guard confirming the fixed-point RPO
// dataflow's own agreement-based merge doesn't overcorrect into never
// warning at all -- if *both* arms of an if/else invalidate the same
// fact, the join must still correctly report "cannot verify" (both
// sides agree the fact is gone, so the merge -- correctly -- has
// nothing to disagree about). See .claude/plans/well-we-last-
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

void both_arms (file *f, bool cond)
{
  open_it (f);
  if (cond)
    mutate_via_alias (f);
  else
    mutate_via_alias (f);
  use_it (f); // { dg-warning "cannot verify" }
}

int main ()
{
  file f;
  both_arms (&f, true);
  return 0;
}
