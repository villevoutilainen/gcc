// D4324/Stage 5: the GIMPLE analogue of d4324-symbolic-proof-predicate-
// field-object-boundary-ok.C -- the same shape as the basic '&h->f'
// repro with no intervening mutation, confirming establish-then-
// consult through cg_field_object_identity's own synthesized key
// actually works on this engine too, not just that invalidation does.
// See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
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

void g (holder *h)
{
  open_it (&h->f);
  use_it (&h->f); // must silently discharge, no diagnostic
}

int main ()
{
  holder h;
  g (&h);
  return 0;
}
