// D4324: symbolic's own call-relational gate (oa_underlying_call_range_
// operand) widens from "accessor must be DECL_DECLARED_CONVEYOR_P" to
// "accessor must be DECL_DECLARED_CONVEYOR_P or DECL_DECLARED_SYMBOLIC_P"
// -- a symbolic function can legitimately be a *member* function (for
// scoping, not because it "really" takes a receiver at runtime), so
// 'v.some_symbolic_axiom ()' inside a symbolic contract's own condition
// is a real shape symbolic's own analysis should recognize, exactly as
// it already does for a conveyor accessor. Never "no restriction at
// all": a plain, untagged accessor still gives the analysis nothing to
// *believe* (no purity guarantee, and this analysis never walks a
// callee's own definition to find out what it does), so it stays
// unrecognized either way. Conveyor's own gate is unchanged (a
// conveyor-flavored contract's condition really executes at runtime, and
// a symbolic accessor -- no definition at all -- could never do that).
// See .claude/plans/lazy-stirring-pearl.md.
//
// Two cases: (1) the new capability, self-trust and consult both
// exercised for a symbolic-declared accessor, plus the same fact run
// through the variable-offset arithmetic tracking; (2) still rejected --
// a plain, untagged accessor named from a symbolic contract is never
// recognized as a checkable obligation at all (contrast case 1's
// *_unchecked, whose identical structure with a symbolic accessor does
// warn).
//
// No third case for "conveyor's own gate is unchanged": confirmed via
// direct testing that this isn't reachable at all -- naming a non-
// conveyor accessor (symbolic or plain) from a *conveyor*-flavored
// predicate is already a hard error at an earlier stage (call.cc's own
// "call to %qD, which is not declared 'conveyor', not permitted in a
// conveyor function or predicate"), independent of this static
// analysis's own gate entirely.  So there is nothing for this file to
// exercise there beyond what already fails to compile.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -fcontract-symbolic-proofs" }

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

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

// Case 1: the new capability -- 'size ()' is symbolic (no definition,
// never really called), named from a symbolic contract.
struct Sym {
  int size () const symbolic;
  int get (int n) const pre<symbolic_ctrl_v>(n < size ()) { return n; }
};

int get_checked (Sym& s, int i) pre<symbolic_ctrl_v>(i < s.size ())
{
  return s.get (i);
}

int get_unchecked (Sym& s, int i)
{
  return s.get (i); // { dg-warning "cannot verify that .i. satisfies" }
}

// Case 1b: the same symbolic accessor, through the variable-offset
// arithmetic tracking built earlier this session -- confirms the offset/
// interval machinery has no hidden conveyor-only assumption.
int use_sound_shift (Sym& s, int i, int k)
  pre<symbolic_ctrl_v>(i < s.size () && k <= 0)
{
  int j = i + k;
  return s.get (j);
}

int use_unsound_shift (Sym& s, int i, int k)
  pre<symbolic_ctrl_v>(i < s.size () && k >= 0 && k <= 2)
{
  int j = i + k;
  return s.get (j); // { dg-warning "cannot verify that .j. satisfies" }
}

// Case 2: still rejected -- 'plain_size ()' has a real body but no
// conveyor/symbolic tag at all; a symbolic contract naming it must not
// be treated as a trackable fact.
struct Plain {
  int plain_size () const { return 5; }
  int get (int n) const pre<symbolic_ctrl_v>(n < plain_size ()) { return n; }
};

int plain_checked (Plain& p, int i) pre<symbolic_ctrl_v>(i < p.plain_size ())
{
  return p.get (i);
  /* No warning: 'plain_size ()' is neither conveyor- nor symbolic-
     declared, so this conjunct is never recognized as a checkable
     obligation at all -- not even to report "cannot verify."  Contrast
     get_unchecked above, whose identical structure with a *symbolic*-
     declared accessor does warn.  */
}

int main ()
{
  Sym s;
  Plain p;
  return get_checked (s, 2) // { dg-warning "cannot verify that .2. satisfies" }
	 + get_unchecked (s, 9)
	 + use_sound_shift (s, 2, -1) // { dg-warning "cannot verify that .2. satisfies" }
	 + use_unsound_shift (s, 2, 1) // { dg-warning "cannot verify that .2. satisfies" }
	 + plain_checked (p, 2);
}
