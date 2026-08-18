// D4324/P2680 Q2: a BY-VALUE parameter (any type, not REFERENCE_TYPE) is
// this function's own independent copy -- exactly as owned as a local
// VAR_DECL it declared itself -- and so may be freely re-lent as a
// non-const reference. This is genuinely different from a REFERENCE_TYPE
// parameter (which aliases the caller's own object and stays borrowed),
// and also different from DEREFERENCING a by-value POINTER parameter:
// the pointer's own storage is owned (it's this function's own copy),
// but what it currently points at is still the caller's own, unrelated
// object, so that stays borrowed. Both distinctions must hold at once,
// which is why this pins them together rather than separately.
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

// Takes the POINTER itself by (non-const) reference -- this is the
// shape that actually exercises Q2 on a by-value pointer parameter's
// own storage, matching std::move's own '_Tp&&' (deduced as '_It&' for
// an lvalue '_It') in basic_const_iterator's real mem-initializer.
T*&
move_ptr (T*& p) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (p))
{
  return p;
}

// LOCAL_COPY is a fresh, independent object this function's own
// pass-by-value created -- owned, may be freely re-lent.
int
accept_by_value_object (T local_copy) conveyor
{
  return use_val_mut (local_copy);
}

// P is a by-value POINTER parameter: P ITSELF (its own storage) is
// owned, but *P (what it currently points at) is the caller's own,
// entirely unrelated object -- still borrowed, exactly as if P were a
// reference parameter. Confirms the by-value exception above doesn't
// leak into pointee ownership.
int
reject_by_value_pointer_dereference (T* p) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (p))
{
  return use_val_mut (*p); // { dg-error "is not owned by the calling function" }
}

// Passing P ITSELF (not *P, a by-value pointer parameter) to a
// reference-TO-POINTER parameter: P's own storage is owned (this
// function's own independent copy), so this must accept even though P
// is a PARM_DECL -- matching the real motivating case (a reference-to-
// pointer parameter, e.g. basic_const_iterator's own mem-initializer
// 'std::move (__current)').
bool
accept_by_value_pointer_itself (T* p) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (p))
{
  T*& r = move_ptr (p);
  return r == p;
}

int
main ()
{
  T t{1};
  accept_by_value_object (t);
  accept_by_value_pointer_itself (&t);
  return 0;
}
