// D4324/Stage 5: the GIMPLE analogue of d4324-symbolic-proof-predicate-
// field-object-direct-write-invalidated-unknown.C -- exercises the
// single-slot invalidation cg_process_field_write now performs (a
// direct write to h->f itself, no call at all, must drop whatever
// predicate fact was tracked about '&h->f' specifically). Unlike the
// AST engine's own Rule 1, cg_process_field_write has no separate
// "whole-object reassignment" branch to worry about conflating with --
// it only ever sees a COMPONENT_REF LHS (an ordinary GIMPLE_ASSIGN's
// own field write), the narrower shape, so this test's own expectation
// isolates exactly that path. See .claude/plans/well-we-last-discussed-
// ethereal-duckling.md.
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
struct holder { file f; };

void open_it (file * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

void g (holder *h, file other_file)
{
  open_it (&h->f);
  h->f = other_file; // direct sub-object replacement, no call involved
  use_it (&h->f); // { dg-warning "cannot verify" }
}

int main ()
{
  holder h;
  file f;
  g (&h, f);
  return 0;
}
