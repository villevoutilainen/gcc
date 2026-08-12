// D4324 Commit 3: the built-in GIMPLE pass's own mirror of d4324-
// conveyor-relational-ifcond.C, for the two shapes it covers there
// (param-vs-param, param-vs-call) -- cg_refine_relational_edge_into's
// own new GIMPLE_COND edge recognition, bridged into the *other*,
// simple linear pass via scalar_rel_cache/scalar_call_rel_cache (see
// cg_predicate_facts_walk's own comment on why). Deliberately no
// call-vs-call case here: see cg_dom_fact_state's own comment on why
// that shape has no dominator-tracked counterpart on the GIMPLE side.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }

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
int get_checked (S& v, int i) conveyor pre<conveyor_ctrl_v>(i < v.size ())
{
  return i;
}

int use_checked_call (S& v, int i) conveyor
{
  if (i < v.size ())
    return get_checked (v, i);
  return -1;
}

int use_unchecked_call (S& v, int i) conveyor
{
  return get_checked (v, i); // { dg-warning "cannot verify that .i. satisfies" }
}

int main ()
{
  S v;
  return use_checked_param (2, 5) + use_unchecked_param (2, 5)
	 + use_checked_call (v, 2) + use_unchecked_call (v, 2);
}
