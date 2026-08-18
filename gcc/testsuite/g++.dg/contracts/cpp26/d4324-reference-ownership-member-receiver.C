// D4324/P2680 Q2 and THIS: the implicit receiver of a non-const member
// conveyor call is a POINTER, and pointers are entirely EXEMPT from Q2,
// in any context (function body or predicate/assert text) -- pointer
// aliasing is categorically the calling function's own concern, never
// the callee's, matching Q1's own pre-existing boundary (a pointer
// never gets an implicit is_object_address obligation either).
//
// This was NOT always the model within this same rework: an earlier
// revision widened Q2 to cover the implicit 'this' receiver (and, more
// broadly, any non-const pointer parameter) uniformly with references.
// That was reverted after direct testing showed it broke the widely-
// used "named predicate" query pattern throughout this whole engine --
// e.g. 'bool is_opened (file* f) conveyor { return f != nullptr; }',
// f an ordinary, read-only, non-const pointer parameter -- since a
// predicate calling such a query with the enclosing function's own
// by-value/pointer parameter as the argument was wrongly rejected (a
// plain pointer parameter is never "received" from a predicate's own
// narrower perspective, the same way a reference parameter isn't). The
// exact gap the widening aimed to close (a fresh pointer bound to a
// directly-named global) is separately, and already, fully closed by
// an independent restriction: conveyor code may never odr-use a
// mutable global at all, in any shape.
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

// obj is a RECEIVED reference parameter: accepted -- re-lending it as
// the receiver of another conveyor call doesn't extend the cone (this
// part is unaffected by the pointer-exemption above: obj.mutate()'s own
// receiver argument is pointer-shaped, hence exempt regardless).
int
accept_received_receiver (T& obj) conveyor
{
  obj.mutate ();
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

// A const member call: fine regardless, Q2 never restricted const
// receivers even when the widened check briefly existed.
int
accept_const_receiver (T& obj) conveyor
{
  return obj.peek ();
}

// A plain, by-value pointer parameter used directly as the receiver:
// accepted -- pointers are exempt from Q2 entirely, so this holds no
// matter what D might alias.
int
accept_pointer_receiver (T* d) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (d))
{
  d->mutate ();
  return d->v;
}

// The motivating "named predicate" pattern: a query taking a plain,
// non-const pointer receiver, called from PRECONDITION TEXT with the
// enclosing function's own by-value pointer parameter -- accepted, in
// both the body and the precondition, since pointers get no ownership
// check in either context.
struct Gauge { bool ready () conveyor { return true; } };

int
accept_pointer_receiver_from_predicate_text (Gauge* g)
  pre<conveyor_ctrl_v>(std::is_object_address (g))
  pre<conveyor_ctrl_v>(g->ready ())
{
  return g->ready () ? 1 : 0;
}

int
main ()
{
  T t{0};
  accept_owned_receiver ();
  accept_const_receiver (t);
  accept_pointer_receiver (&t);
  accept_received_receiver (t);
  t.mutate_via_self ();
  t.mutate_field ();
  Gauge g;
  accept_pointer_receiver_from_predicate_text (&g); // { dg-warning "cannot verify that" }
  return 0;
}
