// D4324/P2680 item 6, Increment I: a callee's postcondition that
// doesn't actually say anything about nonzero-ness or a range
// excluding zero ('r < 1000') must not establish a nonzero fact for
// the caller's stored result -- confirming this is a real conjunct
// match, not a blanket "trust any postcondition" shortcut.
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

int g (int x) conveyor post<conveyor_ctrl_v>(r: r < 1000)
{
  return x;
}

int f (int x) conveyor
{
  int n = g (x);
  return 10 / n; // { dg-error "divisor .n. not provably nonzero in a conveyor function" }
}

int main () { int x = 1; return f (x) - 10; }
