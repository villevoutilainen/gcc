// D4324: reassignment-interaction case for the pointer-aliasing fix --
// q's own alias target is resolved (and canonicalized through
// oa_env::alias_find) eagerly at the moment 'q = p;' runs, not re-
// chased lazily at consult time, so p's own later reassignment ('p =
// &other;') does not retroactively affect what q was already snapshot
// to alias. Rule 1's own invalidation for p's reassignment correctly
// invalidates only p's own (never populated, since p itself already
// aliased f) raw key, leaving f's own established fact -- and q's own
// still-correct claim to it -- untouched. use_it (q) must still
// discharge silently here. See .claude/plans/well-we-last-discussed-
// ethereal-duckling.md.
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
  file f, other;
  file *p = &f;
  open_it (p);
  file *q = p;
  p = &other;
  use_it (q);
  return 0;
}
