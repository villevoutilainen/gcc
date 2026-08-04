// Axiom contracts ("the gem", Mechanism B): -fcontract-symbolic-
// runtime-checks tracking a bare scalar established *inside* a loop
// body and consulted *after* the loop (B3) -- proven true when the
// loop runs at least once (the shadow's own existence, created only in
// oa_handle_loop's SCRATCH copy during its one real pass, must still
// be merged back into the outer env for code after the loop to find
// it), correctly "cannot verify" for zero iterations (the shadow, if
// created at all elsewhere, was never established on this path).  Also
// exercises oa_handle_loop's own per-reassigned-decl re-walks (purely
// compile-time fact computation for other maps) without ever emitting
// duplicate establish code -- see this test's own C file for the
// gimple-dump-based duplication check, kept as a manual/plan-level
// verification rather than a dg-final scan here.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks" }

#include <contracts>
namespace sc = std::contracts;

bool checked = false;
bool failed = false;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  {
    checked = true;
    if (!ctx.check ())
      failed = true;
  }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

int producer () post<symbolic_ctrl_v>(r: r >= 40 && r < 100) { return 55; }
void consumer (int x) pre<symbolic_ctrl_v>(x >= 20 && x < 1000) { (void) x; }

void run (int n)
{
  int y = 0;
  for (int i = 0; i < n; ++i)
    y = producer ();
  consumer (y);
}

int main ()
{
  checked = false; failed = false;
  run (3);
  bool ran_ok = checked && !failed;

  checked = false; failed = false;
  run (0);
  bool zero_iter_unknown = checked && failed;

  if (!ran_ok || !zero_iter_unknown)
    __builtin_abort ();
  return 0;
}
