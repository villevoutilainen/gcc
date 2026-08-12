// D4324: -fcontract-conveyor-proofs' call-vs-call relational shape
// ("RECEIVER_1.CALLEE_1 () OP RECEIVER_2.CALLEE_2 ()", e.g. 'v.size () <
// w.size ()') -- the call-vs-call analogue of the existing param-vs-call
// relational shape (d4324-conveyor-call-relational-basic.C), for two
// calls compared against each other rather than a call against a bare
// parameter. Established only via a caller's own declared precondition
// (self-trust), matching that shape's own scope exactly.
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
};

int use_it (S& a, S& b) conveyor pre<conveyor_ctrl_v>(a.size () < b.size ())
{
  return 0;
}

int get_checked (S& v, S& w) conveyor pre<conveyor_ctrl_v>(v.size () < w.size ())
{
  return use_it (v, w);
}

int get_unchecked (S& v, S& w) conveyor
{
  return use_it (v, w); // { dg-warning "cannot verify that .int S::size\\(\\) const. called on .v." }
}

int main ()
{
  S v, w;
  return get_checked (v, w) // { dg-warning "cannot verify that .int S::size\\(\\) const. called on" }
	 + get_unchecked (v, w);
}
