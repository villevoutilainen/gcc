// D4324: -fcontract-conveyor-proofs' own axis of the same pointer-
// aliasing gap fixed for -fcontract-symbolic-proofs by d4324-symbolic-
// proof-predicate-alias-invalidated-unknown.C -- Rule 2 invalidation
// and oa_env::alias_find are shared by both flavors, so this is caught
// the same way. Here the underlying control-object check still runs at
// runtime regardless of the static prover's own conclusion, so the
// pre-fix version of this bug was "only" a missed diagnostic, not a
// silent runtime failure -- but a missed diagnostic is still the wrong
// answer, and the same fix closes it here too. See .claude/plans/
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
  open_it (p);
  file *q = p;
  mutate_via_alias (q);
  use_it (p); // { dg-warning "cannot verify" }
  return 0;
}
