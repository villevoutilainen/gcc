// Companion to d4324-shifted-call-relational-ok.C: PRODUCER's own
// declared precondition establishes 'idx - v.size () > 10' for its own
// body, but CONSUMER requires the contradicting 'idx - v.size () < 0'
// -- a genuine, provable violation via oa_call_relational_contradicts_p
// (generalized with a REQUIRED_OFFSET parameter for this shift-shaped
// case), not merely "cannot verify".
// { dg-do compile { target c++26 } }
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

struct S {
  int size () const conveyor
    post<conveyor_ctrl_v>(r: r >= 0 && r <= 5)
  { return 5; }
};

// IDX's own range (item 8's mandatory overflow scan needs both operands
// of 'idx - v.size ()' fully bounded to prove the subtraction itself
// free of overflow -- unrelated to what this test is actually about).
int consumer (const S& v, int idx) conveyor
  pre<conveyor_ctrl_v>(idx >= 0 && idx <= 1000 && idx - v.size () < 0)
{
  return idx;
}

int producer (const S& v, int idx) conveyor
  pre<conveyor_ctrl_v>(idx >= 0 && idx <= 1000 && idx - v.size () > 10)
{
  return consumer (v, idx); // { dg-error "provably violates the precondition" }
}

int entry (const S& v, int idx) conveyor
{
  if (idx >= 0 && idx <= 1000 && idx - v.size () > 10)
    {
      producer (v, idx);
      return 0;
    }
  return -1;
}

int main ()
{
  S v;
  return entry (v, 20);
}
