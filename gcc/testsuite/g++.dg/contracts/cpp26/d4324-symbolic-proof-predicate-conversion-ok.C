// D4324: -fcontract-symbolic-proofs' own axis of the same named-
// predicate conversion-operator lookthrough covered for
// -fcontract-conveyor-proofs by d4324-conveyor-proof-predicate-
// conversion-ok.C -- oa_env_predicate_result and oa_object_identity_
// decl are shared by both flavors, so this is discharged the same way.
// See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
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
bool is_opened (file *f) symbolic;

struct file_ref {
  file *f;
  operator file* () const { return f; }
};

void open_it (file * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

int main ()
{
  file f;
  file_ref ref{&f};
  open_it (ref);
  use_it (ref);
  return 0;
}
