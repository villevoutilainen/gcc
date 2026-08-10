// D4324/Stage 3: conveyor-flavor counterpart of the basic parameter-
// alias repro (d4324-symbolic-proof-predicate-parameter-alias-
// invalidated-unknown.C), mirroring how Stages 1/2a/2b's own core
// tests are duplicated across flavors -- m_predicate_fact_map is
// shared substrate either way. See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct conveyor_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr conveyor_ctrl conveyor_ctrl_v{};

struct file { bool opened = false; };
bool is_opened (const file *f) { return f->opened; }

void open_it (file * const f) post<conveyor_ctrl_v> (is_opened (f)) { f->opened = true; }
void mutate_via_alias (file *f) { f->opened = true; }
void use_it (file * const f) pre<conveyor_ctrl_v> (is_opened (f)) { }

void g (file *a, file *b)
{
  open_it (a);
  mutate_via_alias (b);
  use_it (a); // { dg-warning "cannot verify" }
}

int main ()
{
  file f1, f2;
  g (&f1, &f2);
  return 0;
}
