// Companion to d4324-conveyor-array-bound-store-bad.C: the same two
// store-through-a-pointer shapes, but through a provably-valid pointer
// this time -- confirms the LHS scan fix doesn't reject legitimate code.
// { dg-do run { target c++26 } }
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

void deref_store_ok (int *p) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (p))
{
  *p = 5;
}

struct T { int v; };

void field_store_ok (T *p) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (p))
{
  p->v = 5;
}

int main ()
{
  int x = 0;
  deref_store_ok (&x);
  T t { 0 };
  field_store_ok (&t);
  return x == 5 && t.v == 5 ? 0 : 1;
}
