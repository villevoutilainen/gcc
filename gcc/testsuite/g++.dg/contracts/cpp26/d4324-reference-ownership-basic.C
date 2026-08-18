// D4324/P2680 Q2 (the cone-of-evaluation ownership rule): a reference or
// pointer parameter, however validly proven (Q1), is BORROWED from this
// function's own caller and may never be re-lent as a NON-CONST
// reference to a further conveyor call -- only a local this function
// itself created may be. A CONST reference sidesteps the question
// entirely: Q2 only ever restricts non-const re-lending.
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

struct T { int v; };

int use_val_mut (T& x) conveyor { return x.v; }
int use_val_const (const T& x) conveyor { return x.v; }

// A REFERENCE parameter, dereferenced back out to another conveyor
// call's non-const reference parameter: rejected, no intermediate call
// or local at all -- the most direct shape Q2 restricts.
int
reject_ref_param (T& y) conveyor
{
  return use_val_mut (y); // { dg-error "is not owned by the calling function" }
}

// Same, but through a POINTER parameter and an explicit dereference.
int
reject_ptr_param (T* p) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (p))
{
  return use_val_mut (*p); // { dg-error "is not owned by the calling function" }
}

// A CONST reference parameter re-lent as const: Q2 doesn't apply to
// const references at all, only the (already-satisfied) Q1 obligation
// does.
int
accept_const_ref_param (const T& y) conveyor
{
  return use_val_const (y);
}

int
main ()
{
  T t{1};
  accept_const_ref_param (t);
  return 0;
}
