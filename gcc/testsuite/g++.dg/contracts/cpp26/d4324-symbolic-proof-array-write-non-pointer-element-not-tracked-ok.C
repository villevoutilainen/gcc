// D4324/Stage 2b: guards the element-type gate on the write side --
// writing to a non-pointer array's element must not populate
// m_array_alias_target at all (Stage 2b's array-alias detection is
// gated on the element's own type, mirroring Stage 2a's own field-type
// gating). See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
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
  int scalars[3];
  scalars[0] = 42; // non-pointer array write, must not touch arr's own aliases
  use_it (arr[0]); // must silently discharge, no diagnostic
  return 0;
}
