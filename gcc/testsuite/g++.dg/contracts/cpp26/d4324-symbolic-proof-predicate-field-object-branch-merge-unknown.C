// D4324/Stage 5: the branch-merge analogue of every prior stage's own
// branch-merge test -- 'is_opened (&h.f)' is established before the
// 'if', but only the 'if' arm invalidates it via an intervening call;
// the implicit else arm leaves it established. predicate_fact_merge_
// with's own agreement-based semantics must drop the fact after the
// join, since the two arms disagree.
//
// This also exercises the single most important correctness property
// Stage 5's own design review flagged: THEN_ENV/ELSE_ENV are each a
// copy() of the pre-'if' env, so the synthesized placeholder key for
// (h, f) -- already created once, before the fork, by OPEN_IT's own
// postcondition establishment -- must be the *same* tree object in
// both branches (oa_env::m_field_object_key is a shared pointer, never
// deep-copied). If it weren't shared, each branch could, in principle,
// still consult the *same* already-cached key correctly here (the key
// already existed before the fork in this particular test), but a
// design that failed to share the pointer at all would either crash
// (a null cache in a freshly-copied env) or -- if it fell back to a
// separate, freshly-allocated per-copy cache instead -- silently lose
// this and every other already-established field-object fact at the
// very first branch/loop/try fork in any function, a far more visible
// and immediate breakage than the narrower "two arms disagree on the
// key" scenario this file's own name might suggest. See
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
struct holder { file f; };

void open_it (file * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void mutate_via_alias (file *f) { f->opened = false; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

int main (int argc, char **)
{
  holder h;
  open_it (&h.f);
  if (argc)
    mutate_via_alias (&h.f);
  use_it (&h.f); // { dg-warning "cannot verify" }
  return 0;
}
