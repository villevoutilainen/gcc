// D4324: diagnostic-precision demo (oa_unprovable_reason, contracts.h)
// for OA_UNPROVABLE_WRONG_IDENTITY -- unlike d4324-conveyor-proof-
// predicate-unknown.C's OA_UNPROVABLE_NO_FACT (nothing at all
// established), here a real fact *is* established for 'f' (via
// open_it's own postcondition), it's just for a different named
// predicate (is_opened) than the one use_it's own precondition actually
// requires (is_locked) -- oa_env_predicate_result's own
// 'fact.pred_fn != pred_fn' branch.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct conveyor_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr conveyor_ctrl conveyor_ctrl_v{};

struct file { bool opened = false; bool locked = false; };
bool is_opened (const file *f) conveyor { return f != nullptr; }
bool is_locked (const file *f) conveyor { return f != nullptr; }

void open_it (file * const f) post<conveyor_ctrl_v>(is_opened (f)) // { dg-warning "cannot verify postcondition" }
{
  f->opened = true;
}
void use_it (file * const f) pre<conveyor_ctrl_v>(is_locked (f)) { }

void caller ()
{
  file f;
  open_it (&f);
  use_it (&f); // { dg-warning "cannot verify that .*is_locked.*holds" }
               // { dg-message "different object or accessor" "unprovable reason" { target *-*-* } .-1 }
}

int main () { caller (); return 0; }
