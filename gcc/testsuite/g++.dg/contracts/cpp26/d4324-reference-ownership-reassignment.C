// D4324/P2680 Q2's ownership-laundering fix must track a pointer's
// ownership as a real, per-path env fact -- re-derived at every
// reassignment, not just at declaration, and correctly merged (as a
// union: borrowed on either incoming branch stays borrowed) at branch
// joins. A one-shot derivation from the declaration's own initializer
// alone would give the wrong answer the moment the pointer is later
// reassigned to something with different provenance.
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
identity_view_ptr (T* p) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (p))
  post<conveyor_ctrl_v>(r: std::is_object_address (r))
{
  return p;
}

// P starts out owned (pointing at a local), then gets reassigned to a
// borrowed call result -- the borrowed status from the reassignment
// must win, not the stale "owned" from the original declaration.
int
reject_reassign_owned_to_borrowed (T& y) conveyor
{
  T local{5};
  T* p = &local;
  p = identity_view_ptr (&y);
  return use_val_mut (*p); // { dg-error "is not owned by the calling function" }
}

// The reverse: P starts out borrowed, then gets reassigned to an owned
// local -- must accept; a stale borrowed flag must not survive an owned
// reassignment either.
int
accept_reassign_borrowed_to_owned (T& y) conveyor
{
  T local{5};
  T* p = identity_view_ptr (&y);
  p = &local;
  return use_val_mut (*p);
}

// Borrowed on only one arm of an if/else: must still reject after the
// join, confirming the merge is a union (borrowed wins), not an
// intersection or a "keep whichever branch ran last" guess.
int
reject_branch_merge (T& y, bool cond) conveyor
{
  T local{5};
  T* p = &local;
  if (cond)
    p = identity_view_ptr (&y);
  return use_val_mut (*p); // { dg-error "is not owned by the calling function" }
}

int
main ()
{
  T y{2};
  accept_reassign_borrowed_to_owned (y);
  return 0;
}
