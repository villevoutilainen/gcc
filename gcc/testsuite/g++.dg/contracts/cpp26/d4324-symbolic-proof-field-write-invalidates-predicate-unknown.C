// D4324/Stage 4a: a genuine, pre-existing bug found by asking, after
// Stage 3 shipped, whether any gaps remained -- entirely independent
// of aliasing, needing no 'q = p;', no struct-embedded anything: Rule
// 1's own "direct field write" branch (the same COMPONENT_REF block
// Stage 2a extended) only ever called contract_field_range_invalidate
// (the narrow, single-field range map), never predicate_fact_
// invalidate for the whole-object identity, even though a named
// predicate like is_opened is opaque and could depend on any field.
// 'open_it (p); p->opened = false; use_it (p);' (the exact same p, no
// aliasing at all) wrongly verified before this fix. See .claude/
// plans/well-we-last-discussed-ethereal-duckling.md.
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
