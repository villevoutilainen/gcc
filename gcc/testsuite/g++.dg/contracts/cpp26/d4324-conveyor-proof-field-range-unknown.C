// D4324/P2680: -fcontract-conveyor-proofs, ptr->field range proof --
// t's count field was never established via a call to a function whose
// postcondition asserts a range for it, so the best available answer
// is "cannot verify," not silent acceptance, and not a false claim of
// a proven violation either.  See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
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

struct thing {
  int count;
  void consume_count ()
    pre<conveyor_ctrl_v>(this->count >= 20 && this->count < 100)
  { }
};

int main ()
{
  thing t;
  t.count = 50;
  t.consume_count (); // { dg-warning "cannot verify" }
  return 0;
}
