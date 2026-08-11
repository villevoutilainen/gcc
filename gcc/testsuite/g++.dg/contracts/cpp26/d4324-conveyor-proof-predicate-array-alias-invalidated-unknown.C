// D4324/Stage 2b: conveyor-flavor counterpart of the basic array-alias
// repro (d4324-symbolic-proof-predicate-array-alias-invalidated-
// unknown.C), mirroring how Stage 1/2a's own core tests are duplicated
// across flavors -- m_predicate_fact_map and its new m_array_alias_
// target sibling are shared substrate either way. See .claude/plans/
// well-we-last-discussed-ethereal-duckling.md.
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
bool is_opened (const file *f) conveyor { return f != nullptr; }

void open_it (file * const f) post<conveyor_ctrl_v> (is_opened (f)) { f->opened = true; }
void mutate_via_alias (file *f) { f->opened = true; }
void use_it (file * const f) pre<conveyor_ctrl_v> (is_opened (f)) { }

int main ()
{
  file f;
  file *p = &f;
  file *arr[3];
  arr[0] = p;
  open_it (p);
  mutate_via_alias (arr[0]);
  use_it (p); // { dg-warning "cannot verify" }
  return 0;
}
