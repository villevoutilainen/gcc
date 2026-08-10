// D4324/Stage 2b: passing the whole array (decaying to a pointer) to
// an arbitrary call gives the callee 'file **' access capable of
// mutating *any* slot via pointer arithmetic, a different case from
// '*arr'/'arr[0]' (which only ever expose a single element's own
// value, by copy) -- so it needs the same conservative invalidate-all
// treatment as an unprovable-index write. Confirmed via a raw tree
// dump that 'mutate_all (arr);' decays to NOP_EXPR (ADDR_EXPR (arr)) as
// the call argument, which oa_invalidation_identity_decl's own,
// pre-existing ADDR_EXPR branch already resolves to identity = arr
// (with no type gate on the pointee) -- Rule 2 just didn't know to
// sweep m_array_alias_target for it before this fix.
//
// Same "make a regression observable" trick as the other declines-ok
// tests: 'arr[1]' aliases 'g' (never opened) before the whole array is
// passed to mutate_all. If array_alias_invalidate_all correctly fires
// for identity 'arr', 'arr[1]' can no longer resolve and declines (no
// diagnostic, matching this test's own expectation). If a regression
// drops the array_alias_invalidate_all call at this site, 'arr[1]'
// still resolves to 'g', producing an unexpected "cannot verify"
// caught as excess errors. See .claude/plans/well-we-last-discussed-
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
void mutate_all (file **a) { a[0] = nullptr; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

int main ()
{
  file f, g;
  file *p = &f;
  file *arr[3];
  arr[0] = p;
  arr[1] = &g;
  open_it (p);
  mutate_all (arr);
  use_it (arr[1]); // declines to check, no diagnostic -- see comment above
  return 0;
}
