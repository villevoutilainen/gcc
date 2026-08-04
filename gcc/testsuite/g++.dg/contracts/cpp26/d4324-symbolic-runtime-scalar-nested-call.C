// Axiom contracts ("the gem", Mechanism B): a call taking the address of
// an already-shadowed bare scalar must invalidate that shadow even when
// reached only as a call nested *inside* another call's own argument
// list (e.g. the inner 'modify(&y)' in 'foo(modify(&y), 5)'), not just
// when it is itself the RHS's or statement's own top-level call.
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

int modify (int *p) { *p = 1; return *p; }
int foo (int a, int b) { return a + b; }

int main ()
{
  int y = producer ();
  foo (modify (&y), 5);
  consumer (y);
  // y is now 1 (set by the nested modify(&y)), which violates consumer's
  // precondition -- the runtime check must correctly "cannot verify"
  // (checked && failed), not stale-pass on the now-invalid old fact.
  if (!checked || !failed)
    __builtin_abort ();
  return 0;
}
