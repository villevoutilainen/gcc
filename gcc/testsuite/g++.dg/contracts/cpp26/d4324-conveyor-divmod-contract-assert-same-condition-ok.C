// D4324/P2680 item 8: this test originally documented a deliberate,
// conservative scope decision from Increment V -- the div/mod scan for a
// contract_assert/pre/post condition was checked against ENV as it stood
// from *prior* code only, so a later conjunct in the *same* condition
// never benefited from an earlier conjunct establishing the same fact
// (unlike an ordinary if/loop condition, which already had this
// refinement via Increment K). oa_scan_item8_in_expr now gives every
// item-8 call site that same left-to-right, per-conjunct discipline (see
// its own comment, and the real-world report that prompted it:
// https://godbolt.org/z/vjfxK7Psz), so 'n != 0' now correctly seeds the
// nonzero fact '10 / n' needs, and this compiles cleanly.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

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

int f (int n)
{
  contract_assert<conveyor_ctrl_v>(n != 0 && 10 / n > 0);
  return 0;
}

int main () { return f (5); }
