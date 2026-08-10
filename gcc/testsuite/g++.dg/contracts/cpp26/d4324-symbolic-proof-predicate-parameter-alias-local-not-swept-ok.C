// D4324/Stage 3: a local variable that happens to share a parameter's
// pointee type must not trigger the parameter-alias-group sweep at
// all -- oa_invalidate_parameter_alias_group's own guard requires
// IDENTITY to itself be a genuine parameter of the currently-analyzed
// function (DECL_CONTEXT match), so a local's own invalidation stays
// an ordinary, unaffected no-op: this mechanism is specifically about
// two parameters potentially being the *same caller-supplied object*,
// not about a local coincidentally sharing a parameter's type. See
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

void g (file *a)
{
  file other;
  file *local = &other; // a local, of the same pointee type as 'a'
  open_it (a);
  mutate_via_alias (local); // must only ever invalidate 'local'/'other', never 'a'
  use_it (a); // must silently discharge, no diagnostic
}

int main ()
{
  file f1;
  g (&f1);
  return 0;
}
