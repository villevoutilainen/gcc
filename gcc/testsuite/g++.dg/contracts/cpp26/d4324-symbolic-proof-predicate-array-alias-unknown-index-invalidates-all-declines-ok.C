// D4324/Stage 2b: the central new rule this stage adds beyond Stage
// 2a's own model -- unlike a field write (which can only ever affect
// its own, syntactically fixed FIELD_DECL), 'arr[i] = x;' for an
// unprovable 'i' could alias *any* previously-tracked slot of the same
// array, so the write-detection site must sweep every entry for that
// array's own identity (array_alias_invalidate_all), not merely
// decline to update the one slot it can't resolve.
//
// This locks in that rule via the same "make a regression observable
// via an unexpected diagnostic" trick used by the field-alias branch-
// merge test: 'arr[1]' is aliased to 'g' (never opened) before the
// unprovable-index write to 'arr[argc]'. If the sweep correctly drops
// every slot (including index 1, which the write doesn't even
// syntactically name), consulting 'arr[1]' afterward can't resolve any
// identity at all and silently declines (matching this test's own
// expectation of no diagnostic). If a regression only clears the
// specific index actually written (or nothing at all), 'arr[1]' still
// resolves to 'g', whose own is_opened fact was never established,
// producing an unexpected "cannot verify" caught as excess errors. See
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

void open_it (file * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

int main (int argc, char **)
{
  file f, g, g2;
  file *p = &f;
  file *arr[3];
  arr[0] = p;
  open_it (p);
  arr[1] = &g;      // g never opened
  arr[argc] = &g2;  // unprovable index -- must invalidate every slot
  use_it (arr[1]);  // declines to check, no diagnostic -- see comment above
  return 0;
}
