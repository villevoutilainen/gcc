// D4324/P2680 Q2 for THIS: the implicit receiver of a non-const member
// conveyor call needs the same ownership check an ordinary non-const
// reference parameter already gets -- 'obj.mutate ()' (obj a borrowed
// reference parameter) re-lends 'this' non-const exactly like passing a
// borrowed reference to another conveyor call does, but this was never
// checked at all before this fix (DECL_ARGUMENTS's own first parm for a
// member callee is 'this', always POINTER-typed, so the ordinary
// REFERENCE_TYPE-gated loop never reached it).
//
// 'this' itself is owned, not borrowed, though: re-lending it further --
// whether as the receiver of another non-const method on the same
// object, or as an ordinary reference argument derived from '*this' --
// never extends the cone of evaluation the way handing off a genuinely
// borrowed reference does. There's no chance 'this' was invalidated by
// anything reachable from here, and no new party gains access; it's
// still the exact same object this function was already given. A field
// of an owned object (including 'this') is, in turn, itself owned.
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

struct Field { void mutate () conveyor { } };

struct T {
  int v;
  Field f;
  void mutate () conveyor { v = 1; }
  int peek () const conveyor { return v; }
  // Calling another non-const method on the SAME 'this': always fine.
  void mutate_via_self () conveyor { mutate (); }
  // A field of 'this' is itself owned.
  void mutate_field () conveyor { f.mutate (); }
};

// obj is a borrowed reference parameter: rejected.
int
reject_borrowed_receiver (T& obj) conveyor
{
  obj.mutate (); // { dg-error "is not owned by the calling function" }
  return obj.v;
}

// local is genuinely owned: accepted.
int
accept_owned_receiver () conveyor
{
  T local{0};
  local.mutate ();
  return local.v;
}

// A const member call on a borrowed reference: Q2 doesn't apply to
// const receivers at all.
int
accept_const_receiver (T& obj) conveyor
{
  return obj.peek ();
}

// A plain, by-value pointer parameter used directly as the receiver
// (no dereference, no address-of -- 'this' binds D's own value
// directly): accepted, matching a by-value parameter's own storage
// being owned.
int
accept_pointer_receiver (T* d) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (d))
{
  d->mutate ();
  return d->v;
}

int
main ()
{
  T t{0};
  accept_owned_receiver ();
  accept_const_receiver (t);
  accept_pointer_receiver (&t);
  t.mutate_via_self ();
  t.mutate_field ();
  return 0;
}
