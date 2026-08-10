// D4324/Stage 2b: direct analogue of Stage 2a's own field-slot branch-
// merge test (d4324-symbolic-proof-predicate-field-alias-branch-merge-
// declines-ok.C) -- 'arr[0]' aliases 'p' before the if, but only the
// 'if' arm re-targets it to 'g' (never opened); the implicit else arm
// leaves it aliasing 'p' still. array_alias_merge_with's agreement-
// based semantics must drop the (arr, 0) entry entirely after the
// join, since the two arms disagree on the target.
//
// Same reasoning as the field-slot version for why this surfaces as
// silence rather than "cannot verify": array_alias_find has no
// "identity of last resort" fallback (an ARRAY_REF isn't an interned
// decl), so a correctly-dropped entry leaves 'arr[0]' fully
// unresolvable, hitting the same pre-existing "no identity resolved at
// all" decline every consult site already has. Still a meaningful
// regression guard: a wrong (e.g. union-style, or missing-call) merge
// would leave 'arr[0]' resolving to 'g' -- a resolvable identity whose
// own is_opened fact was never established -- producing an unexpected
// "cannot verify" (caught as excess errors) instead of this file's own
// expected silence. See .claude/plans/well-we-last-discussed-ethereal-
// duckling.md.
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

int main (int argc, char **)
{
  file f, g;
  file *p = &f;
  file *arr[3];
  arr[0] = p;
  open_it (p);
  if (argc)
    arr[0] = &g;
  use_it (arr[0]); // declines to check, no diagnostic -- see comment above
  return 0;
}
