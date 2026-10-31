// D4324: after a call, what the callee's postcondition says about the
// objects its pointer and reference parameters designate holds for the
// caller: that a pointer argument now points to an object
// ('post (is_object_address (p))', as a function constructing one there
// promises), or what such an object's value is ('post (*p > 0)', a
// reference parameter's 'post (n != 0)').  Only after the call, on the
// paths it dominates, until the object is written or passed to another
// call; and only from a postcondition whose failed check stops
// execution.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>
namespace sc = std::contracts;
using sc::conveyor_assert_v;
using sc::never_proven_conveyor_v;

struct observing_conveyor
{
  static constexpr bool is_ignored (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool constify (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool assumable (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) noexcept { return true; }
  constexpr void operator() (const sc::assertion_context &) const noexcept { }
};
inline constexpr observing_conveyor observing_v{};

struct T { int v; };
T *get ();
int use (T *p) conveyor pre<conveyor_assert_v> (std::is_object_address (p)) { return p->v; }
void construct (T *const p) post<never_proven_conveyor_v> (std::is_object_address (p));
void construct_observed (T *const p) post<observing_v> (std::is_object_address (p));
void set_ref (int &n) conveyor post<conveyor_assert_v> (n > 0);
void set_ptr (int *const p) conveyor pre<conveyor_assert_v> (std::is_object_address (p))
  post<conveyor_assert_v> (*p != 0);
void set_observed (int &n) conveyor post<observing_v> (n > 0);
void touch (int &n) conveyor;

// Established.
int p1 () { T *m = get (); construct (m); return use (m); }
int p2 (int n) conveyor { set_ref (n); return 10 / n; }
int p3 (int n) conveyor { set_ptr (&n); return 10 / n; }
int p4 (int n) conveyor { set_ref (n); int k = n; return 10 / k; }

// Not established.
int b1 () { T *m = get (); int r = use (m); construct (m); return r; }	// { dg-error "is_object_address" }
int b2 () { T *m = get (); construct_observed (m); return use (m); }	// { dg-error "is_object_address" }
int b3 (int n, int k) conveyor { set_ref (n); n = k; return 10 / n; }	// { dg-error "not provably nonzero" }
int b4 (int n) conveyor { set_ref (n); touch (n); return 10 / n; }	// { dg-error "not provably nonzero" }
int b5 (int n, bool c) conveyor { if (c) set_ref (n); return 10 / n; }	// { dg-error "not provably nonzero" }
int b6 (int n) conveyor { set_observed (n); return 10 / n; }		// { dg-error "not provably nonzero" }
