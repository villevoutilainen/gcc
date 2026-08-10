// D4324/Stage 4c: confirms cg_process_field_write's own ordinary
// GIMPLE_ASSIGN dispatch (a scalar-typed field assigned from a call's
// own return value, 'h->x = make_int();', routes through a temporary
// and a separate, ordinary GIMPLE_ASSIGN -- confirmed via the same
// tree-shape investigation that found the RVO/NRVO GIMPLE_CALL-with-
// COMPONENT_REF-lhs shape, d4324-gimple-symbolic-predicate-rvo-field-
// write-invalidated-unknown.C) correctly resolves identity down to
// 'h' specifically and touches nothing else: a predicate fact
// established on a *different*, untouched object 'h2' must survive.
// H2 is deliberately a local variable, not a second same-typed
// parameter -- Stage 4e's own conservative-by-default parameter-
// alias-group sweep (correctly) treats any two same-typed, non-
// __restrict parameters as a potential-alias group, which would make
// a second 'holder *' parameter an intentionally poor choice here (an
// earlier draft of this test used one and, correctly per that design,
// saw h2's own fact invalidated too). See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs-gimple" }

#include <contracts>
namespace sc = std::contracts;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

struct holder { int x; };
bool is_opened (const holder *h) symbolic;
int make_int () { return 0; }

void open_it (holder * const h) post<symbolic_ctrl_v> (is_opened (h)) { }
void use_it (holder * const h) pre<symbolic_ctrl_v> (is_opened (h)) { }

void g (holder *h)
{
  holder h2;
  open_it (&h2);
  h->x = make_int (); // scalar field write on an unrelated object h
  use_it (&h2); // must silently discharge -- h2 was never touched
}

int main ()
{
  holder h;
  g (&h);
  return 0;
}
