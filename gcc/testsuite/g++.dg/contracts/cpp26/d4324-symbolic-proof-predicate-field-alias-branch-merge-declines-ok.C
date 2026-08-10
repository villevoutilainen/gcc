// D4324/Stage 2a: the field-slot analogue of Stage 1's own branch-
// merge test (d4324-symbolic-proof-predicate-alias-branch-merge-
// unknown.C) -- 'h.ptr' aliases 'p' before the if, but only the 'if'
// arm re-targets it to 'g' (never opened); the implicit else arm
// leaves it aliasing 'p' still. field_alias_merge_with's agreement-
// based semantics (mirroring alias_merge_with's own "keep only if
// both sides agree" rule) must drop the (h, ptr) entry entirely after
// the join, since the two arms disagree on the target.
//
// Unlike Stage 1's own bare-decl case, a correctly-dropped entry here
// doesn't surface as a "cannot verify" diagnostic: field_alias_find
// deliberately has no "identity of last resort" fallback the way
// alias_find does for a bare decl (a COMPONENT_REF isn't itself an
// interned decl to fall back to -- see oa_env::field_alias_find's own
// comment), so once dropped, consulting 'h.ptr' directly can't
// resolve any identity at all, and the consult site's own "no
// identity resolved" path silently declines to check the
// precondition (the same pre-existing, general convention that
// already applies to any untrackable expression, e.g. a plain
// function-call result -- not a new gap Stage 2a introduces).
//
// This is still a meaningful regression guard, not a vacuous one: the
// two arms are deliberately set up so a *wrong* merge (e.g. a missing
// call, or any bug that leaves one arm's value in place unconditionally)
// would leave (h, ptr) resolving to 'g' -- a real, resolvable identity
// whose own is_opened fact was never established -- which would
// produce an *unexpected* "cannot verify" warning (caught as excess
// errors) rather than this file's own expected silence. See
// .claude/plans/well-we-last-discussed-ethereal-duckling.md.
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
struct holder { file *ptr; };

void open_it (file * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

int main (int argc, char **)
{
  file f, g;
  file *p = &f;
  holder h;
  h.ptr = p;
  open_it (p);
  if (argc)
    h.ptr = &g;
  use_it (h.ptr); // declines to check, no diagnostic -- see comment above
  return 0;
}
