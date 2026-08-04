// Axiom contracts ("the gem", Mechanism B): a single precondition
// comparing two different bare parameters must get an independent
// runtime check for *each* one, not just the first bare PARM_DECL any
// conjunct happens to name.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks" }

#include <contracts>
namespace sc = std::contracts;

int check_count = 0;
int fail_count = 0;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  {
    ++check_count;
    if (!ctx.check ())
      ++fail_count;
  }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

int prod_x () post<symbolic_ctrl_v>(r: r >= 40 && r < 100) { return 55; }

void consumer2 (int x, int z)
  pre<symbolic_ctrl_v>(x >= 20 && x < 1000 && z >= 5 && z < 50)
{
  (void) x; (void) z;
}

int main ()
{
  int a = prod_x ();  // established [40,100), subsumed by x's [20,1000)
  int b = 999;         // never established for z, and out of z's [5,50)
  consumer2 (a, b);
  // Both x and z must be independently checked: x's check passes
  // (subsumed), z's has no established fact and is out of range, so it
  // correctly "cannot verify".
  if (check_count != 2 || fail_count != 1)
    __builtin_abort ();
  return 0;
}
