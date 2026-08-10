// D4324/Stage 2b: '*arr' (an array decaying to a pointer and being
// immediately dereferenced) is semantically identical to 'arr[0]' --
// confirmed via a raw tree dump this produces INDIRECT_REF (NOP_EXPR
// (ADDR_EXPR (arr))) as a call argument, a shape found during this
// stage's own design review to be completely unhandled by a first
// draft that only recognized a literal ARRAY_REF. oa_array_slot_base
// recognizes both shapes uniformly, so this ordinary, common idiom is
// caught exactly like the basic 'arr[0]' repro -- unlike the branch-
// merge/whole-array/unknown-index tests above, this one *does*
// resolve to a real identity ('p', consulted directly, which always
// has a resolvable identity of its own), so it surfaces as an
// ordinary "cannot verify", not a decline. See .claude/plans/well-we-
// last-discussed-ethereal-duckling.md.
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
void mutate_via_alias (file *f) { f->opened = false; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

int main ()
{
  file f;
  file *p = &f;
  file *arr[3];
  arr[0] = p;
  open_it (p);
  mutate_via_alias (*arr);
  use_it (p); // { dg-warning "cannot verify" }
  return 0;
}
