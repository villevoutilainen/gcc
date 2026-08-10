// D4324: the companion "ok" case for d4324-conveyor-try-block-bad.C --
// also exercises the fix's own soundness boundary, not just that the
// walk reaches code inside a try/catch at all. open_it()'s postcondition
// establishes is_opened(f); use_it()'s precondition consults it, both
// within the *same* try body -- discharged silently, since an exception
// can only occur *after* open_it() has already run to completion by the
// time use_it() is reached. The second use_it(), *after* the whole
// try/catch, must NOT see that same fact: an exception could have been
// thrown and caught by the handler at any point, including before
// open_it() ever ran, so nothing established inside TRY_STMTS survives
// past the merge with the handler's own (pre-try) state -- confirmed by
// the expected "cannot verify" there, mirroring the same conservative
// N-way merge SWITCH_STMT's own case already relies on. See .claude/
// plans/well-we-last-discussed-ethereal-duckling.md.
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

struct file { bool opened = false; };
bool is_opened (const file *f) { return f->opened; }

void open_it (file * const f) post<conveyor_ctrl_v> (is_opened (f)) { f->opened = true; }
void use_it (file * const f) pre<conveyor_ctrl_v> (is_opened (f)) { }

int main ()
{
  file f;
  try
    {
      open_it (&f);
      use_it (&f);
    }
  catch (...)
    {
      return 1;
    }
  use_it (&f); // { dg-warning "cannot verify" }
  return 0;
}
