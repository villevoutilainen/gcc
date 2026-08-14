// D4324/P2680 item 8: a nonzero-ness precondition ("E != 0",
// oa_nonzero_conjunct_p in contracts.cc) where E is of class type and
// reaches the comparison via its own implicit conversion operator.
// f's own precondition "m != 0" self-trust-seeds a nonzero fact for m;
// dividing by need_nonzero (m)'s own substituted argument is then
// provable without ever resolving wrap's own value. See .claude/
// plans/well-we-last-discussed-ethereal-duckling.md.
// main's own call to f is the unrelated, already-established boundary
// case (a fresh, class-typed argument with no decl or fact behind it
// at all -- same shape and same reason as d4324-conveyor-conversion-
// simple-ok.C's own main() call) and is expected to warn, now that
// oa_handle_call_conveyor_proof_obligation's own nz-conjunct branch
// actually checks a callee's "!= 0" precondition at the call site at
// all (previously silently unchecked for this comparison shape).
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
inline constexpr conveyor_ctrl ctrl_v{};

struct wrap {
  int v;
  constexpr wrap (int v_) : v (v_) {}
  constexpr operator int () const conveyor { return v; }
};

int divide (int a, wrap b) pre<ctrl_v> (b != 0) { return a / b; }

int f (int a, wrap m) pre<ctrl_v> (m != 0)
{
  return divide (a, m);
}

int main () { return f (10, wrap (2)) - 5; } // { dg-warning "cannot verify" }
