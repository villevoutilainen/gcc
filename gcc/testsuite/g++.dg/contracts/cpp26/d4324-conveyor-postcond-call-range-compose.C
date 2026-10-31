// D4324: postcondition-side call-range composition
// (oa_call_postcondition_range_p's own new block, plus its eager
// counterpart oa_compose_call_result_range): a callee's own
// postcondition relating its return value to a call-range-eligible
// accessor ('post<ctrl>(r: r < this->size ())') lets the caller derive
// a concrete range for the call's own result, given an already-
// established call-range fact for the receiver -- including the
// self-referential case (the same call that returns the value also
// exposes the receiver the fact is about), which needs the *eager*,
// pre-invalidation composition specifically: without it, this call's
// own argument invalidation (an inherent, unconditional rule -- any
// call exposing a receiver invalidates facts about it) would drop the
// fact before the lazy, oa_get_range-based path ever got to use it.
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
  int size () const conveyor { return 5; }
  int make () const conveyor post<conveyor_ctrl_v>(r: r < size ()) // { dg-warning "cannot verify postcondition" }
  { return 1; }
};

int consume (int n) conveyor pre<conveyor_ctrl_v>(n < 10) { return n; }

// The self-referential case: s.make()'s own call is what both returns
// y *and* exposes 's' (as its own implicit 'this'), so the guard below
// and the composition both concern the very same call's own receiver.
int use_it (S& s) conveyor
{
  if (s.size () < 10)
    {
      int y = s.make ();
      return consume (y);
    }
  return -1;
}

int use_it_unchecked (S& s) conveyor
{
  int y = s.make ();
  return consume (y); // { dg-warning "cannot verify that .y. satisfies" }
}

int main () { S s; return use_it (s) + use_it_unchecked (s); }
