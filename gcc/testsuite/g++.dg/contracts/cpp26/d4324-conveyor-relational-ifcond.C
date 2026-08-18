// D4324 Commit 3: oa_refine_single_comparison, the shared primitive
// behind every runtime if/ternary condition's own fact refinement, now
// also recognizes the three relational shapes (param-vs-param, param-
// vs-call, call-vs-call) -- previously these were only ever established
// from a declared pre<>, never from an ordinary runtime 'if (...)'
// check, unlike the field-range/call-range shapes (which already got
// this treatment in an earlier commit).
//
// Every 'S' parameter below is a CONST reference: none of these
// functions ever write through it (only S::size(), a const method, is
// ever called), and P2680 9.1's cone-of-evaluation ownership rule (Q2)
// would otherwise make GET_CHECKED/USE_IT's own non-const 'S&'
// parameter impossible to satisfy from a caller that merely proves
// (rather than owns) its own argument -- unrelated to what this test
// is actually about.
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

// param-vs-param.
int check_param (int x, int q) conveyor pre<conveyor_ctrl_v>(x < q) { return x; }

int use_checked_param (int x, int q) conveyor
{
  if (x < q)
    return check_param (x, q);
  return -1;
}

int use_unchecked_param (int x, int q) conveyor
{
  return check_param (x, q); // { dg-warning "cannot verify that .x. satisfies" }
}

struct S {
  int size () const conveyor { return 5; }
};

// param-vs-call.
int get_checked (const S& v, int i) conveyor pre<conveyor_ctrl_v>(i < v.size ())
{
  return i;
}

int use_checked_call (const S& v, int i) conveyor
{
  if (i < v.size ())
    return get_checked (v, i);
  return -1;
}

int use_unchecked_call (const S& v, int i) conveyor
{
  return get_checked (v, i); // { dg-warning "cannot verify that .i. satisfies" }
}

// call-vs-call.
int use_it (const S& a, const S& b) conveyor pre<conveyor_ctrl_v>(a.size () < b.size ())
{
  return 0;
}

int use_checked_call_call (const S& v, const S& w) conveyor
{
  if (v.size () < w.size ())
    return use_it (v, w);
  return -1;
}

int use_unchecked_call_call (const S& v, const S& w) conveyor
{
  return use_it (v, w); // { dg-warning "cannot verify that .int S::size\\(\\) const. called on" }
}

int main ()
{
  S v, w;
  return use_checked_param (2, 5) + use_unchecked_param (2, 5)
	 + use_checked_call (v, 2) + use_unchecked_call (v, 2)
	 + use_checked_call_call (v, w) + use_unchecked_call_call (v, w);
}
