// Axiom contracts (~/gcc-axiom-contracts.md): a pre/post written against
// a control object whose is_symbolic(cfg) returns true generates zero
// runtime code by default -- no predicate thunk, no control-object
// operator() call -- contrasted here with an otherwise identical,
// ordinary (non-symbolic) control object, which must still show its
// usual runtime call.  sym_marker()/normal_marker() are never defined
// (only ever named from inside each control object's own operator()),
// so their presence or absence in the gimple dump directly reflects
// whether that operator() was ever actually invoked anywhere.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fdump-tree-gimple" }

#include <contracts>
namespace sc = std::contracts;

void sym_marker ();
void normal_marker ();

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context&) const { sym_marker (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

struct normal_ctrl {
  void operator() (const sc::assertion_context&) const { normal_marker (); }
};
inline constexpr normal_ctrl normal_ctrl_v{};

bool is_opened (int*) symbolic;

void sym_check (int* p) pre<symbolic_ctrl_v>(is_opened (p)) { (void) p; }
void normal_check (int x) pre<normal_ctrl_v>(x > 0) { (void) x; }

int main () { return 0; }

// { dg-final { scan-tree-dump-not "sym_marker" "gimple" } }
// { dg-final { scan-tree-dump "normal_marker" "gimple" } }
