// D4324: the companion "ok" case for d4324-conveyor-cleanup-stmt-bad.C
// -- open_it()'s postcondition establishes is_opened(f), consulted by
// use_it()'s precondition on the next statement, both statements
// nested inside the CLEANUP_STMT a preceding destructor-needing local
// declaration produces; discharged silently (in-range/proven-true, not
// merely "no diagnostic because nothing was analyzed at all", confirmed
// against d4324-conveyor-cleanup-stmt-bad.C's own use of the same
// destructor-needing local to show a real violation still gets caught
// in this same position). See .claude/plans/well-we-last-discussed-
// ethereal-duckling.md.
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

struct needs_dtor { ~needs_dtor () {} };

struct file { bool opened = false; };
bool is_opened (const file *f) conveyor { return f->opened; }

void open_it (file * const f) post<conveyor_ctrl_v> (is_opened (f)) { f->opened = true; }
void use_it (file * const f) pre<conveyor_ctrl_v> (is_opened (f)) { }

int main ()
{
  needs_dtor guard;
  file f;
  open_it (&f);
  use_it (&f);
  return 0;
}
