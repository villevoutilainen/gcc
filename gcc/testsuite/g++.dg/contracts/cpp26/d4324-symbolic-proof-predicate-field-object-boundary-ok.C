// D4324/Stage 5: the same shape as the basic '&h.f' repro
// (d4324-symbolic-proof-predicate-field-object-invalidated-unknown.C)
// with no intervening mutation at all -- confirms establish-then-
// consult *through* the synthesized field-object identity actually
// works, not just that invalidation does (a silently-declining
// resolver would look identical to a sound fix by output alone in the
// no-mutation case, so this boundary test is what actually
// distinguishes "resolves and verifies" from "still can't resolve at
// all"). See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
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
struct holder { file f; };

void open_it (file * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

int main ()
{
  holder h;
  open_it (&h.f);
  use_it (&h.f); // must silently discharge, no diagnostic
  return 0;
}
