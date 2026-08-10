// D4324/Stage 3: 'file *'/'const file *' parameters must still group
// despite the cv-qualification difference -- confirms oa_could_alias_
// as_parameters compares pointees via TYPE_MAIN_VARIANT (cv-
// insensitive), not a raw type-pointer-equality check, since a caller
// passing the same object through a const and non-const view is
// completely ordinary ('g(p, p)' is legal even though 'a' and 'b' have
// different top-level pointee constness). See .claude/plans/well-we-
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
void mutate_via_alias (const file *f) { const_cast<file *> (f)->opened = false; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

void g (file *a, const file *b)
{
  open_it (a);
  mutate_via_alias (b);
  use_it (a); // { dg-warning "cannot verify" }
}

int main ()
{
  file f1, f2;
  g (&f1, &f2);
  return 0;
}
