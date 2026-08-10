// D4324/Stage 2b: companion to d4324-symbolic-proof-predicate-array-
// alias-invalidated-unknown.C with no intervening mutation -- confirms
// establish-then-consult *through* an array slot actually works
// (oa_array_slot_identity resolving on the consult side), not merely
// that Rule 2 invalidation reaches it. See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
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

int main ()
{
  file f;
  file *p = &f;
  file *arr[3];
  arr[0] = p;
  open_it (p);
  use_it (arr[0]); // must silently discharge, no diagnostic
  return 0;
}
