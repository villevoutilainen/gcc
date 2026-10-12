// D4324/P2680 item 7's Q2 (ownership/cone-of-evaluation): a FIELD of an
// owned object is itself owned -- found via the comprehensive coverage
// sweep (2026-10-12) to be entirely missing from the GIMPLE-pass engine
// (cg_provably_owned_p), which had already documented this exact gap in
// its own leading comment as a deferred follow-up never yet attempted.
// Every realistic trigger is a non-const REFERENCE parameter bound to a
// class-typed (non-pointer) FIELD of an object this function itself
// received or created -- 'this->field' (a member conveyor function), or
// 'ptr->field' (an ordinary pointer parameter) -- both previously
// rejected with "argument ... is not owned by the calling function,
// ... does not name a parameter or local that the calling function
// itself received or created", even though the AST engine (contracts.cc,
// oa_reference_owned_p) already composed this correctly. Confirmed via
// direct testing this was NOT about is_object_address (a separate,
// already-correctly-composed obligation for a field of any type on
// either engine) -- it was specifically Q2 ownership.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

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

struct Inner { int v; };
int use_val_mut (Inner& x) conveyor { x.v = 5; return x.v; }

struct Outer {
  Inner inner;
  int via_this () conveyor { return use_val_mut (inner); }
};

int via_pointer_parm (Outer *op) conveyor
  pre<conveyor_ctrl_v> (std::is_object_address (op))
{
  return use_val_mut (op->inner);
}

int main ()
{
  Outer o1{{1}};
  if (o1.via_this () != 5)
    return 1;
  Outer o2{{1}};
  if (via_pointer_parm (&o2) != 5)
    return 1;
  return 0;
}
