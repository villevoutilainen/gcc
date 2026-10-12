// D4324/P2680: is_object_address composed identically across the three
// shapes an "already-owned object" argument can take at a call site --
// a reference parameter forwarded through ('x'), a by-value
// parameter's own storage ('x'), and an explicit pointer dereference
// already proven by its own precondition ('*p') -- previously tested
// only in disjoint files (reference-only, pointer-only), never
// side-by-side for the same callee/scenario. READS_REF takes its
// argument by CONST reference deliberately: Q2 ownership (a completely
// separate obligation from is_object_address, see d4324-reference-
// ownership-*.C) only ever restricts a NON-const reference parameter,
// so a const one isolates the is_object_address question alone from
// all three shapes -- VIA_POINTER_DEREF in particular would otherwise
// also need to satisfy Q2, which a bare pointer dereference bound to a
// reference parameter never does by design (see oa_reference_owned_p's
// own "a POINTER-typed parameter's bare value... NOT owned, unless
// it's specifically 'this'" comment, contracts.cc) -- a different,
// unrelated restriction this file is not about.
// (An unguarded-pointer negative control is deliberately not repeated
// here -- d4324-conveyor-deref-object-address-nonzero-offset-bad.C
// already covers "an unproven pointer dereference fails," and a hard
// error there would be incompatible with this file's own dg-do run,
// which needs all three cases below to actually compile, link and
// execute to confirm they're not just silently accepted at the
// diagnostic level.)
// { dg-do run { target c++26 } }
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

int reads_ref (const T& r) conveyor
  pre<conveyor_ctrl_v> (std::is_object_address (&r))
{
  return r.v;
}

// (1) Reference parameter, forwarded through.
int via_reference (T& x) conveyor
{
  return reads_ref (x);
}

// (2) By-value parameter: &x is its own, independently-owned storage.
int via_value (T x) conveyor
{
  return reads_ref (x);
}

// (3) Explicit pointer dereference, after its own is_object_address
// precondition proves p.
int via_pointer_deref (T* p) conveyor
  pre<conveyor_ctrl_v> (std::is_object_address (p))
{
  return reads_ref (*p);
}

int main ()
{
  T t{5};
  if (via_reference (t) != 5) return 1;
  if (via_value (t) != 5) return 1;
  if (via_pointer_deref (&t) != 5) return 1;
  return 0;
}
