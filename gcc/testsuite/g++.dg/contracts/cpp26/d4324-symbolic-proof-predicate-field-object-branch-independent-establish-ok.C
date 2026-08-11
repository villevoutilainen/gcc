// D4324/Stage 5: unlike the branch-merge test (which establishes the
// (h, f) placeholder key once, before the fork), this test forces each
// arm of the 'if' to be the *first* to ever ask for a key for the same
// (h, f) pair -- both arms independently call OPEN_IT (&h.f), so if
// oa_env::m_field_object_key were ever changed from a shared pointer to
// a per-instance value (the exact mistake this feature's own design
// review caught and rejected before any code was written -- see
// oa_env::field_object_identity_key's own comment), THEN_ENV and
// ELSE_ENV would each synthesize their OWN, different placeholder tree
// for the same logical slot. After the join, USE_IT's own consult would
// then need to synthesize (or look up) a *third* key from the still-
// shared cache, which -- with the pointer correctly shared -- is the
// exact same key both arms already used, so the fact each arm
// established (identically: 'is_opened' true) survives the merge and
// this discharges silently. A regression back to a per-instance cache
// would instead either crash (a null cache in a freshly-copied env,
// with no explicit propagation) or leave USE_IT unable to find any
// matching key at all, producing an unexpected "cannot verify" here.
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
bool is_opened (const file *f) symbolic;
struct holder { file f; };

void open_it (file * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

int main (int argc, char **)
{
  holder h;
  if (argc)
    open_it (&h.f);
  else
    open_it (&h.f);
  use_it (&h.f); // must silently discharge, no diagnostic
  return 0;
}
