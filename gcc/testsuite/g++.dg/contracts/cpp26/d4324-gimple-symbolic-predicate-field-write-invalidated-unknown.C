// D4324/Stage 4c: the GIMPLE analogue of d4324-symbolic-proof-field-
// write-invalidates-predicate-unknown.C -- the GIMPLE engine had no
// Rule 1 equivalent at all before this stage (its own per-block loop
// only ever inspected is_gimple_call statements; an ordinary GIMPLE_
// ASSIGN, the only way a field write appears in GIMPLE, was invisible
// to it entirely), a more fundamental version of the same gap. Fixed
// via cg_process_field_write, recognizing COMPONENT_REF (MEM_REF
// (base, 0), field) -- MEM_REF stands in for INDIRECT_REF, which
// doesn't exist at the GIMPLE level at all. See .claude/plans/well-
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
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

void g (file *p)
{
  open_it (p);
  p->opened = false; // direct field write, same p, no aliasing, no call
  use_it (p); // { dg-warning "cannot verify" }
}

int main ()
{
  file f;
  g (&f);
  return 0;
}
