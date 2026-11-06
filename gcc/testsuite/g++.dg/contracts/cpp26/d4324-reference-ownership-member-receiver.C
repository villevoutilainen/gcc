// D4324/P2680 Q2 and THIS: the implicit receiver of a non-const member
// conveyor call is a POINTER, and pointers are exempt from Q2 in a
// conveyor function's body -- pointer aliasing is the calling
// function's own concern, matching Q1's boundary (a pointer never gets
// an implicit is_object_address obligation either).
//
// Not in contract condition text: a predicate owns no object it didn't
// create, so it may not call a non-const member function on one, which
// could modify the caller's object (P4445 BUG-15).  A named predicate
// query is const, as is_opened would be; a by-value pointer PARAMETER
// passed to such a query stays exempt.
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

// A non-const query called from PRECONDITION TEXT on what the
// enclosing function's pointer parameter points to: rejected, though the
// body may make the same call.
struct Gauge { bool ready () conveyor { return true; } };

int
reject_non_const_receiver_from_predicate_text (Gauge* g)
  pre<conveyor_ctrl_v>(std::is_object_address (g))
  pre<conveyor_ctrl_v>(g->ready ()) // { dg-error "non-const member function" }
{
  return g->ready () ? 1 : 0;
}

// The named predicate pattern: a const query, accepted.
struct Meter { bool ready () const conveyor { return true; } };

int
accept_const_receiver_from_predicate_text (Meter* m)
  pre<conveyor_ctrl_v>(std::is_object_address (m))
  pre<conveyor_ctrl_v>(m->ready ())
{
  return m->ready () ? 1 : 0;
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
  Meter m;
  accept_const_receiver_from_predicate_text (&m); // { dg-warning "cannot verify that" }
  return 0;
}
