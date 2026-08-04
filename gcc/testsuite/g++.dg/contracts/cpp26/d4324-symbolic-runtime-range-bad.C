// Axiom contracts ("the gem"): -fcontract-symbolic-runtime-checks, the
// pointer-reached comparison shape's not-subsumed case -- produce()'s
// postcondition establishes this->state in [40,100), but consume()'s
// precondition requires this->state in [200,1000); [40,100) is not a
// subset of [200,1000), so the runtime interval-subsumption check must
// fail.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

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

struct widget {
  int state = 0;
  void produce () post<symbolic_ctrl_v>(this->state >= 40 && this->state < 100) {}
  void consume () pre<symbolic_ctrl_v>(this->state >= 200 && this->state < 1000) {}
};

int main ()
{
  widget w;
  w.produce ();
  w.consume ();
  if (!checked || !failed)
    __builtin_abort ();
  return 0;
}
