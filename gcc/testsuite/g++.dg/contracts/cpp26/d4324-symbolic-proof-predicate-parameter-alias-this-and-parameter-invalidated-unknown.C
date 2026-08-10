// D4324/Stage 3: the member-function analogue of the basic parameter-
// alias repro -- 'this' and a same-type parameter are treated as a
// group ('this' needs no special-casing: it is spliced in as the head
// of the real DECL_ARGUMENTS chain for every non-static member
// function, confirmed via method.cc's own build_this_parm and by
// direct tree-dump inspection, so the parameter-group sweep already
// enumerates it). Deliberately an exact-type match ('this' is
// 'file2 * const', 'b' is 'file2 *', same pointee mod cv) -- an
// upcast-related shape ('this' reached through a base-class-typed
// parameter, e.g. inheritance) is a separate, explicitly out-of-scope
// limitation (TYPE_MAIN_VARIANT equality is an exact-type match only,
// not inheritance-aware), not what this test exercises. See
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

struct file2 {
  bool opened = false;
  void g (file2 *b);
};
bool is_opened (const file2 *f) symbolic;

void open_it (file2 * const f) post<symbolic_ctrl_v> (is_opened (f)) { f->opened = true; }
void mutate_via_alias (file2 *f) { f->opened = false; }
void use_it (file2 * const f) pre<symbolic_ctrl_v> (is_opened (f)) { }

void file2::g (file2 *b)
{
  open_it (this);
  mutate_via_alias (b);
  use_it (this); // { dg-warning "cannot verify" }
}

int main ()
{
  file2 s, f2;
  s.g (&f2);
  return 0;
}
