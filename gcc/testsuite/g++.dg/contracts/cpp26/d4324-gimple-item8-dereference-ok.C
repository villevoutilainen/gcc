// Item 8 (array-bounds/pointer-arithmetic) mandatory UB scan, GIMPLE
// side, "Increment 1" (dereference-provability only -- see cg_check_
// dereference_ub's own leading comment for why the full array-offset-
// tracking machinery isn't ported). Also caught by contracts.cc's own
// mandatory, unconditional item 8 pass (gated purely on the enclosing
// function being conveyor, no proof flag needed), so this only
// demonstrates "compilation succeeds with both engines active," matching
// item 8's own div/mod and overflow testing limitation -- see this
// directory's own item8-divmod/item8-overflow tests. The dereference-
// through-a-store case is different: see d4324-gimple-item8-dereference-
// store-bad.C, a case AST's own mandatory pass does NOT catch at all.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

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

int deref_ok (int *p) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (p))
{
  return *p;
}

int deref_local_ok () conveyor
{
  int x = 5;
  int *p = &x;
  return *p;
}

int deref_ref_ok (int &r) conveyor
{
  return r;
}

struct T { int v; };
int deref_field_ok (T *p) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (p))
{
  return p->v;
}

int main ()
{
  int x = 0;
  T t { 0 };
  return deref_ok (&x) + deref_local_ok () + deref_ref_ok (x)
	 + deref_field_ok (&t);
}
