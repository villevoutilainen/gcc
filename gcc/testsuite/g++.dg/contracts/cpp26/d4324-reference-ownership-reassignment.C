// D4324/P2680 Q2's ownership-laundering fix must track a pointer's
// ownership as a real, per-path env fact -- re-derived at every
// reassignment, not just at declaration, and correctly merged (as a
// union: borrowed on either incoming branch stays borrowed) at branch
// joins. A one-shot derivation from the declaration's own initializer
// alone would give the wrong answer the moment the pointer is later
// reassigned to something with different provenance.
//
// BORROWED source: dereferencing a by-value pointer parameter (Q's own
// pointee is still borrowed post-rework, see d4324-reference-ownership-
// basic.C), laundered through a CONST reference parameter -- Q2 never
// restricts const targets, so the call itself is legal, but the
// callee's own return value is still correctly inferred as borrowed via
// oa_call_result_owned_p, which inspects every pointer/reference-typed
// argument regardless of the callee's own parameter constness. (A
// directly-named global would have been a simpler source, but conveyor
// code may never odr-use a mutable global at all -- a separate,
// stricter, unrelated restriction -- so it can't reach this analysis to
// test it.)
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

T*
identity_view_ptr_from_const_ref (const T& cr) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (&cr))
  post<conveyor_ctrl_v>(r: std::is_object_address (r))
{
  return const_cast<T*> (&cr);
}

// P starts out owned (pointing at a local), then gets reassigned to a
// borrowed call result -- the borrowed status from the reassignment
// must win, not the stale "owned" from the original declaration.
int
reject_reassign_owned_to_borrowed (T* q) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (q))
{
  T local{5};
  T* p = &local;
  p = identity_view_ptr_from_const_ref (*q);
  return use_val_mut (*p); // { dg-error "is not owned by the calling function" }
}

// The reverse: P starts out borrowed, then gets reassigned to an owned
// local -- must accept; a stale borrowed flag must not survive an owned
// reassignment either.
int
accept_reassign_borrowed_to_owned (T* q) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (q))
{
  T local{5};
  T* p = identity_view_ptr_from_const_ref (*q);
  p = &local;
  return use_val_mut (*p);
}

// Borrowed on only one arm of an if/else: must still reject after the
// join, confirming the merge is a union (borrowed wins), not an
// intersection or a "keep whichever branch ran last" guess.
int
reject_branch_merge (T* q, bool cond) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (q))
{
  T local{5};
  T* p = &local;
  if (cond)
    p = identity_view_ptr_from_const_ref (*q);
  return use_val_mut (*p); // { dg-error "is not owned by the calling function" }
}

int
main ()
{
  T t{2};
  accept_reassign_borrowed_to_owned (&t);
  return 0;
}
