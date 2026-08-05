// symbolic_proof_plugin.cc: produce_count_bad()'s postcondition
// establishes this->count in [200,300), fully disjoint from
// consume_count()'s required [20,100) -- a genuine, provable
// violation.  See .claude/plans/well-we-last-discussed-ethereal-
// duckling.md.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

struct thing {
  int count;
  void produce_count_bad ()
    post<symbolic_ctrl_v>(this->count >= 200 && this->count < 300)
  { count = 250; }
  void consume_count ()
    pre<symbolic_ctrl_v>(this->count >= 20 && this->count < 100)
  { }
};

void caller ()
{
  thing t;
  t.produce_count_bad ();
  t.consume_count (); // { dg-error "provably violates the precondition" }
}

int main () { caller (); return 0; }
