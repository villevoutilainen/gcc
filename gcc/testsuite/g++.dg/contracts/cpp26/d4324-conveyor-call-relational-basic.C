// D4324: -fcontract-conveyor-proofs' call-relational shape ("PARAM OP
// RECEIVER.ACCESSOR ()", e.g. 'i < v.size ()') -- the call analogue of
// the existing paramA-vs-paramB relational shape, and the shape that
// actually motivated this whole feature (the plain call-range shape,
// d4324-conveyor-call-range-basic.C, only recognizes a call compared
// against a *literal*, e.g. 'v.size () > 3' -- a materially different,
// narrower shape found not to cover 'n < v.size ()' at all). Both
// established sides exercised: a caller's own declared precondition
// (self-trust) and an ordinary caller passing an unconstrained literal.
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
  int get (int n) const conveyor pre<conveyor_ctrl_v>(n < size ()) { return n; }
};

int get_checked (S& s, int i) conveyor pre<conveyor_ctrl_v>(i < s.size ())
{
  return s.get (i);
}

int get_unchecked (S& s, int i) conveyor
{
  return s.get (i); // { dg-warning "cannot verify that .i. satisfies" }
}

int main ()
{
  S s;
  return get_checked (s, 2) // { dg-warning "cannot verify that .2. satisfies" }
	 + get_unchecked (s, 9);
}
