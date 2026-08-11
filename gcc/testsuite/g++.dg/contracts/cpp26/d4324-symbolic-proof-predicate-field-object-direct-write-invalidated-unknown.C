// D4324/Stage 5: the single-slot field_object_predicate_invalidate
// path -- no call at all, just a direct write replacing the sub-object
// ('h.f = file ();'), the same shape Rule 1's own COMPONENT_REF write
// branch already narrowly invalidates contract_field_range_invalidate
// for. Since '&h.f' names a fixed address but not a fixed *value*,
// replacing the sub-object it points to must drop whatever predicate
// fact was tracked about that address, the same narrower-than-whole-
// object granularity contract_field_range_invalidate already uses.
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

int main ()
{
  holder h;
  file other_file;
  open_it (&h.f);
  h.f = other_file; // direct sub-object replacement, no call involved
  use_it (&h.f); // { dg-warning "cannot verify" }
  return 0;
}
