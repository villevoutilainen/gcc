// D4324/Stage 4c: a real gap found by design review, fixed in the same
// revision as the basic GIMPLE field-write fix -- a class/aggregate-
// typed field written from a call's own return value under mandatory
// copy elision ('h->b = make_big();') is *not* a GIMPLE_ASSIGN at all:
// confirmed via raw tree dump it's a GIMPLE_CALL whose own LHS *is*
// the COMPONENT_REF directly ('gimple_call <make_big, h_2(D)->b>'),
// a shape neither gimple_assign_single_p (requires is_gimple_assign,
// false for a GIMPLE_CALL) nor the pre-existing call-args-only
// invalidation (which only ever inspected gimple_call_arg, never
// gimple_call_lhs) could see -- missed by the first draft entirely,
// caught before any code was written. See .claude/plans/well-we-last-
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

struct big_thing { int data[100]; };
struct holder { big_thing b; };
bool is_opened (const holder *h) symbolic;
big_thing make_big () { return big_thing (); }

void open_it (holder * const h) post<symbolic_ctrl_v> (is_opened (h)) { }
void use_it (holder * const h) pre<symbolic_ctrl_v> (is_opened (h)) { }

void g (holder *h)
{
  open_it (h);
  h->b = make_big (); // RVO/NRVO: a GIMPLE_CALL whose own LHS is h->b
  use_it (h); // { dg-warning "cannot verify" }
}

int main ()
{
  holder h;
  g (&h);
  return 0;
}
