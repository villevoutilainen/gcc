// D4324: named-predicate identity, given a pointer alias, previously
// invalidated the wrong decl -- "Rule 2" (oa_invalidate_symbolic_facts_
// for_call_args) invalidates a call argument's own syntactic decl
// identity, not what it points to, so 'q = p; mutate_via_alias (q);'
// never invalidated the fact open_it()'s postcondition established via
// 'p', even though p and q name the same object. For -fcontract-
// symbolic-proofs specifically (is_opened here is a pure axiom,
// declared but never defined, so there is no runtime backstop the way
// a conveyor contract's own control-object check provides), this was a
// silent, undetectable correctness failure -- confirmed by direct
// testing that this exact reproduction compiled with zero diagnostics
// before the fix (oa_env::alias_find canonicalizes every predicate/
// field-range establish/consult/invalidate site through a pointer's
// current alias target). See .claude/plans/well-we-last-discussed-
// ethereal-duckling.md.
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
void mutate_via_alias (file *f) { f->opened = false; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

int main ()
{
  file f;
  file *p = &f;
  open_it (p);
  file *q = p;
  mutate_via_alias (q);
  use_it (p); // { dg-warning "cannot verify" }
  return 0;
}
