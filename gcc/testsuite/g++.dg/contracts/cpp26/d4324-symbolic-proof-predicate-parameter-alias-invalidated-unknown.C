// D4324/Stage 3: two of a function's own distinct parameters are never
// treated as potentially the same object anywhere in this file, even
// though nothing in standard C++ prevents a caller from passing the
// same object through two different pointer parameters ('f(p, p)' is
// completely ordinary, legal code) unless a parameter is __restrict-
// qualified. Unlike Stages 1/2a/2b (which each fix a different
// syntactic source of *observed* aliasing within one function body),
// 'a'/'b' here are never assigned from one another at all -- the gap
// is that within a single function's own body walk, two same/
// compatible-typed, non-__restrict parameters must always be treated
// as *possibly* the same object. See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
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

void g (file *a, file *b)
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
