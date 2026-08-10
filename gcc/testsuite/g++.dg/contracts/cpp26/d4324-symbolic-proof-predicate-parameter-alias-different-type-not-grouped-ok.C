// D4324/Stage 3: two parameters of unrelated types must not be
// grouped -- oa_could_alias_as_parameters's own TYPE_MAIN_VARIANT
// comparison correctly declines for 'file*' vs 'widget*'. Guards the
// type-match gate against being overly conservative. See .claude/
// plans/well-we-last-discussed-ethereal-duckling.md.
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
struct widget { int x; };
bool is_opened (const file *f) symbolic;

void open_it (file * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void mutate_widget (widget *w) { w->x = 0; }
void use_it (file * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

void g (file *a, widget *b)
{
  open_it (a);
  mutate_widget (b);
  use_it (a); // must silently discharge, no diagnostic
}

int main ()
{
  file f1;
  widget w1;
  g (&f1, &w1);
  return 0;
}
