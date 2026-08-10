// D4324: the built-in GIMPLE-pass engine's own conveyor-flavored axis
// of the same pointer-aliasing gap -- see d4324-gimple-symbolic-
// predicate-alias-invalidated-unknown.C's own header for the fix
// itself; this is the "missed diagnostic, real check still runs"
// severity level conveyor gets, mirroring d4324-conveyor-proof-
// predicate-alias-invalidated-unknown.C's own AST-engine analogue. See
// .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }

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
