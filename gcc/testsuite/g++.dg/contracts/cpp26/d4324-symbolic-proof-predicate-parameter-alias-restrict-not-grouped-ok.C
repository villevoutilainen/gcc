// D4324/Stage 3: __restrict is the existing, standard way to opt out
// of the new conservative-by-default parameter-alias grouping -- a
// parameter declared __restrict is a caller-facing promise that
// nothing else accessible aliases it, so oa_could_alias_as_parameters
// must never group it with anything, in either direction. See
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
void mutate_via_alias (file *f) { f->opened = false; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

void g (file * __restrict a, file *b)
{
  open_it (a);
  mutate_via_alias (b);
  use_it (a); // must silently discharge, no diagnostic
}

int main ()
{
  file f1, f2;
  g (&f1, &f2);
  return 0;
}
